// tree-sitter -> ast adapter.
// unlike the old pointer ast (deleted), nodes live in a flat astlist arena and
// children are stored BY VALUE in consecutive slots :
//   decl : arena[idx]     = name,  arena[idx+1] = type, arena[idx+2] = value
//   op   : arena[idx]     = left,  arena[idx+1] = right
//   block: arena[start..] = children, terminated by an astNull node
// indices are 1-based astidx into the arena (see ast.h). a node's own entry
// is pushed by its parent (or by parse_source2 for the root) and nested child
// runs always land in the arena BEFORE their parent's slots, keeping every
// block's run of children contiguous. absent children are stored as astNull,
// which srcpos_ast maps to 0 so the ?: chains in ast.h fall through.
#include "ast.h"
#include "wheels/allocator.h"
#include "wheels/macros.h"
#include <string.h>
#include <tree-sitter/lib/include/tree_sitter/api.h>

extern const TSLanguage *tree_sitter_cshl(void);

static ast null_ast() { return (ast)tu_of(astNull, {}); }

// register the byte range of a ts node as a srcpos (1-based index into plist)
static srcpos register_pos(poslist *plist, u32 fileIndex, TSNode n) {
  u32 start = ts_node_start_byte(n);
  sglist_push(*plist, {.fidx = fileIndex, .offset = start, .length = ts_node_end_byte(n) - start});
  return (srcpos)plist->len;
}

// converts a ts node into its ast2 value. the node's child slots are pushed
// into alist before returning, so the returned value is a single entry that
// its parent stores by value.
static ast ts_to_ast2 errs(allocfn allocator, astlist *alist, poslist *plist, u32 fileIndex, const u8 *source, TSNode ts_node) {
  if (ts_node_is_null(ts_node) || !ts_node_is_named(ts_node) || ts_node_is_error(ts_node) || ts_node_is_missing(ts_node))
    return_err_extras("PARSE_ERROR", snprint(allocator, "Syntax error at byte {}", ts_node_start_byte(ts_node)).ptr);

  let type = ts_node_type(ts_node);

  if (!strcmp(type, "empty_statement"))
    return (ast)tu_of(astEmpty, register_pos(plist, fileIndex, ts_node));

  else if (!strcmp(type, "id") || !strcmp(type, "number") || !strcmp(type, "string")) {
    let pos = register_pos(plist, fileIndex, ts_node);
    if (!strcmp(type, "id")) return (ast)tu_of(astId, pos);
    else if (!strcmp(type, "number")) return (ast)tu_of(astNumber, pos);
    else if (!strcmp(type, "string")) return (ast)tu_of(astString, pos);
    else unreachable();
  }

  else if (!strcmp(type, "decl")) {
    TSNode name_node = ts_node_child_by_field_name(ts_node, "name", 4);
    TSNode type_node = ts_node_child_by_field_name(ts_node, "type", 4);
    TSNode value_node = ts_node_child_by_field_name(ts_node, "value", 5);
    TSNode assign_op = ts_node_child_by_field_name(ts_node, "assignment_op", 13);

    ast name = !ts_node_is_null(name_node) ? try_err(ts_to_ast2, (allocator, alist, plist, fileIndex, source, name_node)) : null_ast();
    ast type_ = !ts_node_is_null(type_node) ? try_err(ts_to_ast2, (allocator, alist, plist, fileIndex, source, type_node)) : null_ast();
    ast value = !ts_node_is_null(value_node) ? try_err(ts_to_ast2, (allocator, alist, plist, fileIndex, source, value_node)) : null_ast();

    // name, type and value all next to each other starting at off
    astidx off = (astidx)alist->len + 1;
    sglist_push(*alist, name);
    sglist_push(*alist, type_);
    sglist_push(*alist, value);

    return (ast)tu_of(astDecl, {.name_type_value = off, .isconst = !ts_node_is_null(assign_op) ? source[ts_node_start_byte(assign_op)] == '~' : false});
  }

  else if (!strcmp(type, "op")) {
    TSNode left_node = ts_node_child_by_field_name(ts_node, "left", 4);
    TSNode right_node = ts_node_child_by_field_name(ts_node, "right", 5);
    TSNode op_node = ts_node_child_by_field_name(ts_node, "operator", 8);

    ast left = !ts_node_is_null(left_node) ? try_err(ts_to_ast2, (allocator, alist, plist, fileIndex, source, left_node)) : null_ast();
    ast right = !ts_node_is_null(right_node) ? try_err(ts_to_ast2, (allocator, alist, plist, fileIndex, source, right_node)) : null_ast();

    // left and right next to each other
    astidx off = (astidx)alist->len + 1;
    sglist_push(*alist, left);
    sglist_push(*alist, right);

    ast op = (ast)tu_of(astOp, {.left_right = off, .number_rep = 0});
    if (!ts_node_is_null(op_node)) {
      u32 start = ts_node_start_byte(op_node);
      u32 len = ts_node_end_byte(op_node) - start;
      assertMessage(len <= sizeof(u32), "operator too long");
      memcpy(op.astOp.symliteral, source + start, len);
    } else op.astOp.number_rep = ((union {char s[sizeof(u32)]; u32 u; }){"()"}).u; // call operator

    return op;
  }

  else if (!strcmp(type, "program") || !strcmp(type, "pblock") || !strcmp(type, "bblock") || !strcmp(type, "ablock") || !strcmp(type, "nblock")) {
    u32 count = ts_node_named_child_count(ts_node);

    // convert children first : their own child runs land in the arena before
    // this node's run, keeping arena[start..terminator] contiguous
    let roots = *acreate(allocator, arrof(ast, count ?: 1));
    u32 rc = 0;
    for (u32 i = 0; i < count; i++) {
      let child = ts_node_named_child(ts_node, i);
      if (!strcmp(ts_node_type(child), "comment")) continue;
      roots[rc++] = try_err(ts_to_ast2, (allocator, alist, plist, fileIndex, source, child));
    }

    astidx start = (astidx)alist->len + 1;
    for (u32 i = 0; i < rc; i++)
      sglist_push(*alist, roots[i]);
    sglist_push(*alist, null_ast()); // null-terminator

    let pos = register_pos(plist, fileIndex, ts_node);
    return (ast)(!strcmp(type, "program") || !strcmp(type, "bblock") ? (ast)tu_of(astBBlock, {.pos = pos, .start = start})
                 : !strcmp(type, "ablock")                           ? (ast)tu_of(astABlock, {.pos = pos, .start = start})
                                                                     : (ast)tu_of(astPBlock, {.pos = pos, .start = start}));
  }

  assertMessage(false, "unhandled node %s", type);
}

// parses input into the ast2 arena and returns the root index along with the
// lists needed to resolve srcpos (ast nodes, positions and files). safe to
// call multiple times (appends to the same lists).
astidx_full parse_source2 errs(allocfn out_allocator, fptr input, const char *filename) {
  astidx_full res = {
      .alist = {.allocator = out_allocator},
      .plist = {.allocator = out_allocator},
      .flist = {.allocator = out_allocator},
  };

  // register the source as a "file" so srcpos can later be resolved back to
  // file/line/col and to the original text
  sglist_push(res.flist, {.filename = filename ?: "<source>", .filecontent = input, .fileptr = nullptr});
  u32 fileIndex = (u32)res.flist.len; // 1-based, see fp_from_srcpos

  TSParser *parser = ts_parser_new();
  defer { ts_parser_delete(parser); };
  ts_parser_set_language(parser, tree_sitter_cshl());
  TSTree *tree = ts_parser_parse_string(parser, NULL, (const char *)input.ptr, input.len);
  defer { ts_tree_delete(tree); };

  let root = try_err(ts_to_ast2, (out_allocator, &res.alist, &res.plist, fileIndex, input.ptr, ts_tree_root_node(tree)));
  sglist_push(res.alist, root); // the root is referenced by no parent slot
  res.idx = (astidx)res.alist.len;
  return res;
}