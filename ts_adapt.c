#include "ast.h"
#include "wheels/macros.h"
#include <string.h>
#include <tree-sitter/lib/include/tree_sitter/api.h>

extern const TSLanguage *tree_sitter_cshl(void);

static ast *ts_to_ast(allocfn allocator, const u8 *source, TSNode ts_node) {
  if (ts_node_is_null(ts_node)) return nullptr;

  if (ts_node_is_error(ts_node) || ts_node_is_missing(ts_node))
    error_at((u8 *)source + ts_node_start_byte(ts_node), "Syntax error encountered");

  if (!ts_node_is_named(ts_node)) return 0;
  const char *type = ts_node_type(ts_node);

  if (!strcmp(type, "empty_statement"))
    return avalue(allocator, (ast)tu_of(astEmptyStatement, {}));
  else if (!strcmp(type, "id") || !strcmp(type, "number") || !strcmp(type, "string")) {
    u32 start = ts_node_start_byte(ts_node);
    u32 end = ts_node_end_byte(ts_node);
    fptr ptr_data = {end - start, (u8 *)source + start};

    if (!strcmp(type, "id")) return avalue(allocator, (ast)tu_of(astId, ptr_data));
    if (!strcmp(type, "number")) return avalue(allocator, (ast)tu_of(astNumber, ptr_data));
    return avalue(allocator, (ast)tu_of(astString, ptr_data));
  } else if (!strcmp(type, "decl")) {
    TSNode name_node = ts_node_child_by_field_name(ts_node, "name", 4);
    TSNode type_node = ts_node_child_by_field_name(ts_node, "type", 4);
    TSNode value_node = ts_node_child_by_field_name(ts_node, "value", 5);
    TSNode assign_op = ts_node_child_by_field_name(ts_node, "assignment_op", 13);

    let decl = (astDecl){};

    if (!ts_node_is_null(name_node)) decl.name = ts_to_ast(allocator, source, name_node);
    if (!ts_node_is_null(type_node)) decl.type = ts_to_ast(allocator, source, type_node);
    if (!ts_node_is_null(value_node)) decl.value = ts_to_ast(allocator, source, value_node);
    if (!ts_node_is_null(assign_op))
      decl.is_const = source[ts_node_start_byte(assign_op)] == '~';

    return avalue(allocator, (ast)tu_of(astDecl, decl));
  } else if (!strcmp(type, "op")) {
    TSNode left_node = ts_node_child_by_field_name(ts_node, "left", 4);
    TSNode right_node = ts_node_child_by_field_name(ts_node, "right", 5);
    TSNode op_node = ts_node_child_by_field_name(ts_node, "operator", 8);

    ast *left_ast = ts_node_is_null(left_node) ? 0 : ts_to_ast(allocator, source, left_node);
    ast *right_ast = ts_node_is_null(right_node) ? 0 : ts_to_ast(allocator, source, right_node);

    let opNode = avalue(allocator, (ast)tu_of(astOp, {.left = left_ast, .right = right_ast, .number_rep = 0}));

    if (!ts_node_is_null(op_node)) {
      u32 start = ts_node_start_byte(op_node);
      u32 len = ts_node_end_byte(op_node) - start;
      assertMessage(len <= sizeof(u32), "operator too long");
      memcpy(opNode->astOp.symLiteral, source + start, len);
    } else opNode->astOp.number_rep = ((union {char s[sizeof(u32)] ; u32 u ; }){"()"}).u;

    return opNode;
  } else if (!strcmp(type, "program") || !strcmp(type, "pblock") || !strcmp(type, "bblock") || !strcmp(type, "ablock")) {
    u32 count = ts_node_named_child_count(ts_node);
    u8 *loc = (u8 *)(source + ts_node_start_byte(ts_node));

    let rp = *acreate(allocator, arrof(ptrof(ast), count + 3));

    if (!strcmp(type, "pblock"))
      rp[0] = avalue(allocator, (ast)tu_of(astPBlock, ((astPBlock){loc, rp + 1})));
    else if (!strcmp(type, "ablock"))
      rp[0] = avalue(allocator, (ast)tu_of(astABlock, ((astABlock){loc, rp + 1})));
    else rp[0] = avalue(allocator, (ast)tu_of(astBBlock, ((astBBlock){loc, rp + 1})));

    for (u32 i = 0; i < count; i++)
      rp[i + 1] = ts_to_ast(allocator, source, ts_node_named_child(ts_node, i));
    rp[count + 1] = 0;

    return rp[0];
  } else assertMessage(false, "unknown node");
}
ast *parse_source(allocfn out_allocator, fptr input) {
  TSParser *parser = ts_parser_new();
  defer { ts_parser_delete(parser); };
  ts_parser_set_language(parser, tree_sitter_cshl());
  TSTree *tree = ts_parser_parse_string(parser, NULL, (const char *)input.ptr, input.len);
  defer { ts_tree_delete(tree); };
  TSNode root_node = ts_tree_root_node(tree);

  return ts_to_ast(out_allocator, input.ptr, root_node);
}
