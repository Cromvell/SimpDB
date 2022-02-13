#pragma once

// TODO: Rewrite parsing with expected_token function, and check how it turns out
// TODO: Add support for template type parameters
// TODO: (?)Add support for reference parameters

struct Function_Argument {
  char *type_name = nullptr;
  s32 type_name_length = -1;
  u32 pointer_level = 0;
  bool is_const = false;
  bool is_compound_type = false;
};

struct Function_Declaration {
  char *function_name = nullptr;
  s32 name_length = -1;

  Array<Function_Argument> arguments;
};

void init(Function_Declaration * decl);
void deinit(Function_Declaration *decl);

enum Declaration_Token_Kind : u8 {
  UNKNOWN_TOKEN      = 0x01,
  EOS                = 0x02,
  TYPE               = 0x04,
  COMMA_SEPARATOR    = 0x08,
  STAR               = 0x10,
  OPENING_PARENTHESE = 0x20,
  CLOSING_PARENTHESE = 0x40,
  CONST_SPECIFIER    = 0x80,
};

struct Declaration_Token {
  Declaration_Token_Kind kind;

  char *name = nullptr;
  s32 length = -1;
};

Declaration_Token get_next_token(char *search_start);
Function_Declaration parse_function_declaration(char *string_start, u32 string_length);
