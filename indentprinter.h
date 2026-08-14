#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include <stdio.h>
#include <wheels/print.h>

void indentout(const char *chars, void *instate, usize len, bool flush) {
  static thread_local let state_st = struct_imm(
      (indent_level, (u32)0),
      (at_line_start, true),
      (pending_break, false)
  );
  typeof(&state_st) state = instate ? instate : &state_st;

  foreach (const char *c, span(chars, len)) {
    // Ignore spaces, tabs, and separator slashes.
    if (*c == ' ' || *c == '\t' || *c == '/')
      continue;

    // If a break is pending and the next token is not a comma, insert newline + indent.
    if (state->pending_break) {
      if (*c != ',') {
        if (!state->at_line_start) {
          fputc('\n', stdout);
          state->at_line_start = true;
        }
      }
      state->pending_break = false;
    }

    switch (*c) {
      case '{':
        if (state->at_line_start) {
          // Start of line: indent, then brace.
          foreach (let i, range(0, state->indent_level))
            fputc(' ', stdout);
          state->at_line_start = false;
          fputc('{', stdout);
        } else {
          // Already content on line: separate with a space, then brace.
          fputc(' ', stdout);
          fputc('{', stdout);
        }
        state->indent_level++;
        state->pending_break = true;
        break;

      case '}':
        // Always put closing brace on its own line.
        if (!state->at_line_start) {
          fputc('\n', stdout);
          state->at_line_start = true;
        }
        if (state->indent_level > 0)
          state->indent_level--;
        foreach (let i, range(0, state->indent_level))
          fputc(' ', stdout);
        fputc('}', stdout);
        state->at_line_start = false; // we just printed a character
        state->pending_break = true;
        break;

      case ',':
        // Comma stays on current line; set a pending break for the next token.
        if (state->at_line_start) {
          foreach (let i, range(0, state->indent_level))
            fputc(' ', stdout);
          state->at_line_start = false;
        }
        fputc(',', stdout);
        state->pending_break = true;
        break;

      default:
        if (state->at_line_start) {
          foreach (let i, range(0, state->indent_level))
            fputc(' ', stdout);
          state->at_line_start = false;
        }
        fputc(*c, stdout);
        break;
    }
  }

  if (flush)
    fflush(stdout);
}

static void sexplike_puts(char *c) {
  puts(c);
}
static void sexplike_putc(char c) {
  putchar(c);
}

static void sexplike_escape(char c) {
  switch (c) {
    case '\\':
      sexplike_putc('\\');
      sexplike_putc('\\');
      break;
    case '"':
      sexplike_putc('\\');
      sexplike_putc('"');
      break;
    case '\n':
      sexplike_putc('\\');
      sexplike_putc('n');
      break;
    case '\r':
      sexplike_putc('\\');
      sexplike_putc('r');
      break;
    case '\t':
      sexplike_putc('\\');
      sexplike_putc('t');
      break;
    default:
      sexplike_putc(c);
      break;
  }
}

typedef struct {
  FILE *out;
  int depth;
  bool inString;
  bool escapeNext;
} SExprPrinterState;

static thread_local SExprPrinterState tl_defaultState = {
    .out = NULL, .depth = 0, .inString = false, .escapeNext = false
};

static void emitIndent(SExprPrinterState *st) {
  for (int i = 0; i < st->depth; i++)
    fputs("  ", st->out);
}

void sexplike(const char *data, void *userdata, usize len, bool flush) {
  SExprPrinterState *st = userdata ? (SExprPrinterState *)userdata : &tl_defaultState;
  if (!st->out) st->out = stdout;

  for (unsigned long i = 0; i < len; i++) {
    char c = data[i];

    if (st->inString) {
      fputc(c, st->out);
      if (st->escapeNext) st->escapeNext = false;
      else if (c == '\\') st->escapeNext = true;
      else if (c == '"') st->inString = false;
      continue;
    }

    switch (c) {
      case '"':
        st->inString = true;
        fputc(c, st->out);
        break;

      case '(':
        fputc('(', st->out);
        st->depth++;
        break;

      case ')':
        st->depth--;
        fputc(')', st->out);
        break;

      case ',':
        fputc(',', st->out);
        fputc('\n', st->out);
        emitIndent(st);
        break;

      default:
        fputc(c, st->out);
    }
  }

  if (flush) {
    fflush(st->out);
  }
}
