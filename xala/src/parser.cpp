#include "parser.h"
#include "common.h"
#include "vmintrin.h"
#include "debug.h"

#define UNHANG {static int c = 0; if (c++ > 1000) {CHECKOUT(1);}}
#define CPINSTR_MAX (1 << 12)
static Instr current_prog_instrs[CPINSTR_MAX];
static Program current_prog;

struct LineInfo {
  uint line, col;
  Span span;
};

struct Label {
  Span name;
  uint ip;
};
#define LABEL_COUNT 128
struct LabelInfo {
  Label labels[128];
  uint labels_len;
};

enum TokenType {
  TokenType_Null,
  TokenType_Immediate, // number
  TokenType_Register, 
  TokenType_Newline,
  TokenType_Instr, 
  TokenType_Label
};

struct Token {
  TokenType type;
  const char *str;
  uint len, line, col;
};

struct Parser {
  Token last_token;
  bool putlab;
  LabelInfo labels;
  const char *source;
  bool going;
  uint line, col;
};


LineInfo tok2li(Token t) {
  return LineInfo{t.line, t.col, {t.str, t.len}};
}

void putval(LineInfo li) {
  if (li.span.l == 0 || *li.span.s == 0) {
    tprintf("{}:{} ::", li.line+1, li.col+1);
  } else {
    tprintf("{}:{} Near `{}` ::", li.line+1, li.col+1, li.span);
  }
}

bool labels_put(Parser *p, LabelInfo *li, Token t) {
  if (!p->putlab) {
    return 0;
  }
  (void)p;
  if (t.type != TokenType_Label) {
    tprintf("ERROR: {} Is not a label\n", tok2li(t));
    CHECKOUT(1);
  }

  t.str += 1;
  t.len -= 1;

  if (span_equal(Span{"MAIN", 4}, Span{t.str, t.len})) {
    current_prog.start = current_prog.instrs_len;
  }

  for (int i = 0; i < li->labels_len; ++i) {
    if (span_equal(li->labels[i].name, Span{t.str, t.len})) {
      tprintf("ERROR: {} Label redefined\n", tok2li(t));
      CHECKOUT(1);
    }
  }

  if (li->labels_len >= 128) {
    tprintf("ERROR: {} Too many labels\n", tok2li(t));
    CHECKOUT(1);
  }
  li->labels[li->labels_len++] = Label{Span{t.str, t.len}, current_prog.instrs_len};
  return 0;
}

bool labels_get(Parser *p, LabelInfo *li, Token t, uint *out_ip) {
  if (p->putlab) {
    return 0;
  }
  if (t.type != TokenType_Instr) {
    tprintf("ERROR: {} Is not a label\n", tok2li(t));
    CHECKOUT(1);
  }

  for (int i = 0; i < li->labels_len; ++i) {
    if (span_equal(li->labels[i].name, Span{t.str, t.len})) {
      *out_ip = li->labels[i].ip;
      return 0;
    }
  }

  tprintf("ERROR: {} Label or instruction doesn't exist\n", tok2li(t));
  CHECKOUT(1);
}

u8 parser_get(Parser *p) {
  if (*p->source == '\0') {
    p->going = false;
    return 0;
  }
  return *p->source;
}

bool parser_is(Parser *p, u8 against) {
  return *p->source == against && *p->source != 0;
}

bool parser_isnt(Parser *p, u8 against) {
  return *p->source != against && *p->source != 0;
}

u8 parser_next(Parser *p) {
  u8 old = *p->source;
  if (old == '\0') {
    p->going = false;
    return 0;
  }

  if (old == '\n') {
    p->line += 1;
    p->col = 0;
  } else {
    p->col += 1;
  }
  p->source++;
  return old;
}

static bool skip_empty(Parser *p) {
  if (parser_is(p, ' ') || parser_is(p, '\r') || parser_is(p, '\t')) {
    parser_next(p);
    return true;
  } else if (parser_is(p, ';')) {
    while (parser_isnt(p, '\n')) {
      parser_next(p);
    }
    return true;
  }

  return false;
}

static bool isalpha(u8 c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool isnum(u8 c) {
  return c >= '0' && c <= '9';
}

static bool isalnum(u8 c) {
  return isnum(c) || isalpha(c);
}

static bool fetch_name(Parser *p, Token res) {
  int l = 0;
  while (isalnum(parser_get(p))) {
    parser_next(p);
    l++;
  }
  if (l <= 0) {
    tprintf("ERROR: {} Name can't be empty\n", tok2li(res));
    return true;
  }
  return false;
}

static bool fetch_token(Parser *p, Token *output) {
  while (skip_empty(p)) 
    ;



  Token res = {};
  res.line = p->line;
  res.col = p->col;
  res.str = p->source;

  if (parser_is(p, '%')) {
    res.type = TokenType_Register;
    parser_next(p);
    CHECKOUT(fetch_name(p, res));
  } else if (parser_is(p, '@')) {
    res.type = TokenType_Label;
    parser_next(p);
    CHECKOUT(fetch_name(p, res));
  } else if (parser_is(p, '\n')) {
    res.type = TokenType_Newline;
    parser_next(p);
  } else if (parser_is(p, '-') || isnum(parser_get(p))) {
    if (parser_is(p, '-')) {
      parser_next(p);
    }
    res.type = TokenType_Immediate;
    while (isnum(parser_get(p))) {
      parser_next(p);
    }
    if (parser_is(p, '.')) {
      parser_next(p);
      while (isnum(parser_get(p))) {
        parser_next(p);
      }
    }
  } else if (isalpha(parser_get(p))) {
    res.type = TokenType_Instr;
    CHECKOUT(fetch_name(p, res));
  } else if (p->going == false) {

  } else {
    res.len = 1;
    tprintf("ERROR: {} Invalid character\n", tok2li(res));
    return true;
  }


  res.len = p->source-res.str;
  *output = res;

  p->last_token = res;
  return false;
}

bool parser_put_instr(Parser *p, Instr instr) {
  (void)p;
  if (current_prog.instrs_len >= CPINSTR_MAX) {
    tprintf("ERROR: Too many instructions\n");
    return true;
  }
  current_prog.instrs[current_prog.instrs_len++] = instr;
  return false;
}

float token_to_number(Token t) {
  float res = 0;
  float frac = 0;
  float div = 10;
  float sign = 1;
  if (*t.str == '-') {
    sign = -1;
    t.str++;
  }

  while (isnum(*t.str)) {
    res = res * 10 + (*t.str++ - '0');
  }
  if (*t.str == '.') {
    t.str++;
    while (isnum(*t.str)) {
      frac += (*t.str++ - '0')/div;
      div *= 10;
    }
  }

  return (res + frac) * sign;
}

bool token_to_register(Token t, Reg *reg_out) {
  if (t.type != TokenType_Register) {
    tprintf("ERROR: {} Expected register\n", tok2li(t));
    CHECKOUT(1);
  }

  Span registers[REG_COUNT];
  registers[Reg_X] = Span{"X", 1};
  registers[Reg_Y] = Span{"Y", 1};
  registers[Reg_Ret] = Span{"RET", 3};
  registers[Reg_Out] = Span{"OUT", 3};
  registers[Reg_Time] = Span{"TIME", 4};
  registers[Reg_Base] = Span{"BASE", 4};
  registers[Reg_Memory] = Span{"MEMORY", 6};
  registers[Reg_A] = Span{"A", 1};
  t.str += 1; // skip %
  t.len -= 1; //
  for (uint i = 0; i < REG_COUNT; ++i) {
    if (span_equal(registers[i], Span{t.str, t.len})) {
      *reg_out = (Reg)i;
      return 0;
    }
  }

  tprintf("ERROR: {} Unknown register\n", tok2li(t));
  CHECKOUT(1);
}

bool emit_value(Parser *p, Token t) {
  switch (t.type) {
    case TokenType_Immediate: {
      float fv = token_to_number(t);
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Imm, *reinterpret_cast<uint*>(&fv)}));
    } break;
    case TokenType_Register: {
      if (isnum(t.str[1])) {
        t.str += 1;
        t.len -= 1;
        float fv = token_to_number(t);

        CHECKOUT(parser_put_instr(p, Instr{InstrType_Copy, uint(fv)}));
      } else {
        Reg reg;
        CHECKOUT(token_to_register(t, &reg));
        CHECKOUT(parser_put_instr(p, Instr{InstrType_Load, reg}));
      }
    } break;
    default: {
      tprintf("ERROR: {} Can not be used as a value\n", tok2li(t));
      CHECKOUT(1);
    } break;
  }

   return 0;
}

bool arg_count(Token t, int act, int exp) {
  if (act > exp) {
    tprintf("ERROR: {} Too many arguments (got {} expected {})\n", tok2li(t), act, exp);
    CHECKOUT(1);
  } else if (act < exp) {
    tprintf("ERROR: {} Too few arguments (got {} expected {})\n", tok2li(t), act, exp);
    CHECKOUT(1);
  }
  return 0;
}

bool put_branch(Parser *p, InstrType branchtype, Token namelabel) {
  uint ip;
  CHECKOUT(labels_get(p, &p->labels, namelabel, &ip));
  CHECKOUT(parser_put_instr(p, Instr{branchtype, ip}));
  return 0;
}

bool parse_call(Parser *p, Token name, int argc, bool *pop) {
  if (name.type != TokenType_Instr) {
    tprintf("ERROR: {} Is not an instruction\n", tok2li(name));
    CHECKOUT(1);
  }

  if (span_equal({name.str, name.len}, {"BA", 2})) {
    Token t;
    CHECKOUT(fetch_token(p, &t));
    CHECKOUT(put_branch(p, InstrType_Ba, t));
    CHECKOUT(arg_count(name, argc+1, 1));
    *pop = false;
    return 0;
  } else if (span_equal({name.str, name.len}, {"BP", 2})) {
    Token t;
    CHECKOUT(fetch_token(p, &t));
    CHECKOUT(put_branch(p, InstrType_Bp, t));
    CHECKOUT(arg_count(name, argc+1, 2));
    *pop = false;
    return 0;
  } else if (span_equal({name.str, name.len}, {"BN", 2})) {
    Token t;
    CHECKOUT(fetch_token(p, &t));
    CHECKOUT(put_branch(p, InstrType_Bn, t));
    CHECKOUT(arg_count(name, argc+1, 2));
    *pop = false;
    return 0;
  } else if (span_equal({name.str, name.len}, {"BZ", 2})) {
    Token t;
    CHECKOUT(fetch_token(p, &t));
    CHECKOUT(put_branch(p, InstrType_Bz, t));
    CHECKOUT(arg_count(name, argc+1, 2));
    *pop = false;
    return 0;
  } else if (span_equal({name.str, name.len}, {"BNZ", 3})) {
    Token t;
    CHECKOUT(fetch_token(p, &t));
    CHECKOUT(put_branch(p, InstrType_Bnz, t));
    CHECKOUT(arg_count(name, argc+1, 2));
    *pop = false;
    return 0;
  } else if (span_equal({name.str, name.len}, {"INTO", 4})) {
    Token t;
    CHECKOUT(fetch_token(p, &t));
    Reg reg;
    CHECKOUT(token_to_register(t, &reg));
    CHECKOUT(parser_put_instr(p, Instr{InstrType_Store, reg}));
    CHECKOUT(fetch_token(p, &name));
    CHECKOUT(arg_count(name, argc+1, 2));
    *pop = false;
    return 0;
  } else {
    // TODO: Handle too many/few params
again:
    Token t;
    CHECKOUT(fetch_token(p, &t));

    if (t.type == TokenType_Immediate || t.type == TokenType_Register) {
      argc++;
      CHECKOUT(emit_value(p, t));
      goto again;
    }

    if (span_equal({name.str, name.len}, {"RET", 3})) {
      CHECKOUT(arg_count(name, argc, 0));
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Ret}));
    } else if (span_equal({name.str, name.len}, {"ADD", 3})) {

      CHECKOUT(arg_count(name, argc, 2));
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Add}));

    } else if (span_equal({name.str, name.len}, {"SUB", 3})) {

      CHECKOUT(arg_count(name, argc, 2));
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Sub}));

    } else if (span_equal({name.str, name.len}, {"MOD", 3})) {

      CHECKOUT(arg_count(name, argc, 2));
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Mod}));

    } else if (span_equal({name.str, name.len}, {"MUL", 3})) {

      CHECKOUT(arg_count(name, argc, 2));
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Mul}));

    } else if (span_equal({name.str, name.len}, {"DIV", 3})) {

      CHECKOUT(arg_count(name, argc, 2));
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Div}));

    } else if (span_equal({name.str, name.len}, {"SIN", 3})) {

      CHECKOUT(arg_count(name, argc, 1));
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Sin}));

    } else if (span_equal({name.str, name.len}, {"COS", 3})) {

      CHECKOUT(arg_count(name, argc, 1));
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Cos}));

    } else if (span_equal({name.str, name.len}, {"POW", 3})) {

      CHECKOUT(arg_count(name, argc, 2));
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Pow}));

    } else if (span_equal({name.str, name.len}, {"PRINT", 5})) {

      CHECKOUT(arg_count(name, argc, 1));
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Print, name.line}));

      *pop = false;
      return 0;
    } else {
      uint ip;
      CHECKOUT(parser_put_instr(p, Instr{InstrType_SetBase, uint(argc)}));
      CHECKOUT(labels_get(p, &p->labels, name, &ip));
      CHECKOUT(parser_put_instr(p, Instr{InstrType_Call, ip}));
    }

    name = t;
  }

  if (p->going && name.type == TokenType_Instr) {
    CHECKOUT(parse_call(p, name, 1, pop));
  }
  return 0;
}

bool parse_tape(Parser *p) {
  Token t;
  CHECKOUT(fetch_token(p, &t));
  int argc = 0;
  if (t.type == TokenType_Label) {
    CHECKOUT(labels_put(p, &p->labels, t));
    return 0;
  }
  bool pop = false;
  if (t.type == TokenType_Immediate || t.type == TokenType_Register) {
    CHECKOUT(emit_value(p, t));
    CHECKOUT(fetch_token(p, &t));
    pop = true;
    argc++;
  }
  if (!(t.type == TokenType_Null || t.type == TokenType_Newline)) {
    CHECKOUT(parse_call(p, t, argc, &pop));
    if (!(p->last_token.type == TokenType_Null || p->last_token.type == TokenType_Newline)) {
      tprintf("ERROR: {} Expected newline or end of file\n", tok2li(p->last_token));
      return 1;
    }
  }

  if (pop) {
    parser_put_instr(p, Instr{InstrType_Pop, 1});
  }

  return 0;
}

bool parse(Parser *p, Program *output, const char *source) {
  current_prog.instrs = current_prog_instrs;
  current_prog.instrs_len = 0;
  while (p->going) {
    if (parse_tape(p)) {
      // prevent bad program
      current_prog.instrs[0].type = InstrType_Exit;
      current_prog.instrs_len = 1;
      return 1;
    }
  }
  parser_put_instr(p, Instr{InstrType_Exit});
  *output = current_prog;
  return false;
}

bool parser_parse(Program *output, const char *source) {
  Parser p = {};
  current_prog = {};
  p.putlab = true;
  p.source = source;
  p.going = *source;

  CHECKOUT(parse(&p, output, source));

  p.line = 0;
  p.col = 0;
  p.source = source;
  p.going = *source;
  p.putlab = false;
  CHECKOUT(parse(&p, output, source));

  return 0;
}
