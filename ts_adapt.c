#include "ast.h"
#include "wheels/allocator.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <tree-sitter/lib/include/tree_sitter/api.h>

extern const TSLanguage *tree_sitter_cshl(void);

static ast null_ast() { return (ast)tu_of(astNull, {}); }

static srcpos register_pos(poslist *plist, u32 fileIndex, TSNode n) {
  u32 start = ts_node_start_byte(n);
  sglist_push(*plist, {.fidx = fileIndex, .offset = start, .length = ts_node_end_byte(n) - start});
  return (srcpos)plist->len;
}

static ast ts_to_ast2 errs(allocfn allocator, astlist *alist, poslist *plist, filelist *flist, u32 fileIndex, const u8 *source, TSNode ts_node) {
  if (ts_node_is_null(ts_node) || !ts_node_is_named(ts_node) || ts_node_is_error(ts_node) || ts_node_is_missing(ts_node))
    pass_err(error_loc_add(allocator,
        ((err_t){.err_code = "PARSE_ERROR", .file = __FILE__, .line = __LINE__, .function = __FUNCTION__}),
        ((srcpos_full){register_pos(plist, fileIndex, ts_node), *plist, *flist})), (ast){});

  if (ts_node_has_error(ts_node)) {
    TSNode bad = ts_node;
    while (!ts_node_is_error(bad) && !ts_node_is_missing(bad)) {
      TSNode next = {};
      for (u32 i = 0; i < ts_node_child_count(bad); i++) {
        let child = ts_node_child(bad, i);
        if (ts_node_has_error(child) || ts_node_is_error(child) || ts_node_is_missing(child)) {
          next = child;
          break;
        }
      }
      if (ts_node_is_null(next)) break;
      bad = next;
    }
    pass_err(error_loc_add(allocator,
        ((err_t){.err_code = "PARSE_ERROR", .file = __FILE__, .line = __LINE__, .function = __FUNCTION__}),
        ((srcpos_full){register_pos(plist, fileIndex, bad), *plist, *flist})), (ast){});
  }

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

    ast name = !ts_node_is_null(name_node) ? try_err(ts_to_ast2, (allocator, alist, plist, flist, fileIndex, source, name_node), (ast){}) : null_ast();
    ast type_ = !ts_node_is_null(type_node) ? try_err(ts_to_ast2, (allocator, alist, plist, flist, fileIndex, source, type_node), (ast){}) : null_ast();
    ast value = !ts_node_is_null(value_node) ? try_err(ts_to_ast2, (allocator, alist, plist, flist, fileIndex, source, value_node), (ast){}) : null_ast();

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

    ast left = !ts_node_is_null(left_node) ? try_err(ts_to_ast2, (allocator, alist, plist, flist, fileIndex, source, left_node), (ast){}) : null_ast();
    ast right = !ts_node_is_null(right_node) ? try_err(ts_to_ast2, (allocator, alist, plist, flist, fileIndex, source, right_node), (ast){}) : null_ast();

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

    let roots = *acreate(allocator, arrof(ast, count ?: 1));
    u32 rc = 0;
    for (u32 i = 0; i < count; i++) {
      let child = ts_node_named_child(ts_node, i);
      if (!strcmp(ts_node_type(child), "comment")) continue;
      roots[rc++] = try_err(ts_to_ast2, (allocator, alist, plist, flist, fileIndex, source, child), (ast){});
    }

    astidx start = (astidx)alist->len + 1;
    for (u32 i = 0; i < rc; i++)
      sglist_push(*alist, roots[i]);
    sglist_push(*alist, null_ast());

    let pos = register_pos(plist, fileIndex, ts_node);
    return (ast)(!strcmp(type, "program") || !strcmp(type, "bblock") ? (ast)tu_of(astBBlock, {.pos = pos, .start = start})
                 : !strcmp(type, "ablock")                           ? (ast)tu_of(astABlock, {.pos = pos, .start = start})
                                                                     : (ast)tu_of(astPBlock, {.pos = pos, .start = start}));
  }

  assertMessage(false, "unhandled node %s", type);
}

astidx_full parse_source errs(allocfn out_allocator, fptr input, const char *filename) {
  assertMessage(!filename + fptr_isNull(input) == 1);
  astidx_full res = {
      .alist = out_allocator,
      .plist = out_allocator,
      .flist = out_allocator,
  };
  let const fileit =
      filename
          ? (fileItem){
                .filename = filename,
                .filecontent = read_file(out_allocator, filename),
            }
          : (fileItem){
                .filecontent = input,
            };
  // clang-format off
  sglist_push(res.flist, fileit);
  // clang-format on
  u32 fileIndex = (u32)res.flist.len; // 1-based, see fp_from_srcpos
  fptr content = fileit.filecontent;

  TSParser *parser = ts_parser_new();
  defer { ts_parser_delete(parser); };
  ts_parser_set_language(parser, tree_sitter_cshl());
  TSTree *tree = ts_parser_parse_string(parser, NULL, (const char *)content.ptr, content.len);
  defer { ts_tree_delete(tree); };

  let root = try_err(ts_to_ast2, (out_allocator, &res.alist, &res.plist, &res.flist, fileIndex, content.ptr, ts_tree_root_node(tree)), (astidx_full){});
  sglist_push(res.alist, root); // the root is referenced by no parent slot
  res.idx = (astidx)res.alist.len;
  return res;
}
