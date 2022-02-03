#pragma once

#include "Array.h"

// TODO: Add support for template type parameters
// TODO: Add support for keywords, like const

struct Function_Argument {
  char *type_name = nullptr;
  s32 type_name_length = -1;
  u32 pointer_level = 0;
};

struct Function_Declaration {
  char *function_name = nullptr;
  s32 name_length = -1;

  Array<Function_Argument> arguments;
};

void init(Function_Declaration * decl) {
}

void deinit(Function_Declaration *decl) {
  decl->arguments.deinit();
}

enum Declaration_Token_Kind : u8 {
  UNKNOWN_TOKEN      = 0x01,
  EOS                = 0x02,
  TYPE               = 0x04,
  COMMA_SEPARATOR    = 0x08,
  STAR               = 0x10,
  OPENING_PARENTHESE = 0x20,
  CLOSING_PARENTHESE = 0x40,
};

struct Declaration_Token {
  Declaration_Token_Kind kind;

  char *name = nullptr;
  s32 length = -1;
};

Declaration_Token get_next_token(char *search_start) {
  switch (*search_start) {
  case '(':
    return (Declaration_Token){.kind=OPENING_PARENTHESE, .length=1};
  case ')':
    return (Declaration_Token){.kind=CLOSING_PARENTHESE, .length=1};
  case ',':
    return (Declaration_Token){.kind=COMMA_SEPARATOR, .length=1};
  case '*':
    return (Declaration_Token){.kind=STAR, .length=1};
  case '\0':
    return (Declaration_Token){.kind=EOS, .length=1};
  default: {
    char *pointer = search_start;

    if (!(isalpha(*pointer) || *pointer == '_'))  return (Declaration_Token){.kind=UNKNOWN_TOKEN, .length=-1};

    while (isalnum(*pointer) || *pointer == '_')  pointer++;

    return (Declaration_Token){.kind=TYPE, .name=(char *)search_start, .length=static_cast <s32>(pointer - search_start)};
  }
  }
}

Function_Declaration parse_function_declaration(char *string_start, u32 string_length) {
  Function_Declaration result;

  char *token_pointer = string_start;

  // @Note: Think about enforcing correctness of function identifier
  while ((token_pointer - string_start) < string_length && !isspace(*token_pointer) && *token_pointer != '(')  token_pointer++;

  result.function_name = string_start;
  result.name_length = token_pointer - string_start;

  while ((token_pointer - string_start) < string_length && *token_pointer != '(')  token_pointer++; // Skip to the opening parenthese

  if ((result.name_length + 1) >= string_length) {
    return result;
  } else {
    auto token = get_next_token(token_pointer);
    if (token.kind != Declaration_Token_Kind::OPENING_PARENTHESE)  return result;

    auto previous_token = token;
    token_pointer = token_pointer + previous_token.length;

    while (isspace(*token_pointer)) token_pointer++; // Skip spaces

    token = get_next_token(token_pointer);

    u8 stop_flags = (Declaration_Token_Kind::CLOSING_PARENTHESE | Declaration_Token_Kind::EOS | Declaration_Token_Kind::UNKNOWN_TOKEN);
    while (!(token.kind & stop_flags)) {
      switch (token.kind) {
      case Declaration_Token_Kind::TYPE:
        if (previous_token.kind & (Declaration_Token_Kind::COMMA_SEPARATOR | Declaration_Token_Kind::OPENING_PARENTHESE)) {
          result.arguments.add((Function_Argument){.type_name=token.name, .type_name_length=token.length});
        } else {
          return (Function_Declaration){};
        }
        break;

      case Declaration_Token_Kind::STAR:
        if (result.arguments.count <= 0)  return (Function_Declaration){};

        if (previous_token.kind == Declaration_Token_Kind::TYPE) {
          result.arguments.back().pointer_level = 1;
        } else if (previous_token.kind == Declaration_Token_Kind::STAR && result.arguments.back().pointer_level > 0) {
          result.arguments.back().pointer_level++;
        } else {
          return (Function_Declaration){};
        }
        break;

      case Declaration_Token_Kind::COMMA_SEPARATOR:
        if (previous_token.kind & (Declaration_Token_Kind::TYPE | Declaration_Token_Kind::STAR)) {
        } else {
          return (Function_Declaration){};
        }
        break;

      case Declaration_Token_Kind::OPENING_PARENTHESE:
        return (Function_Declaration){};
      }
      
      previous_token = token;
      token_pointer = token_pointer + previous_token.length;

      while (isspace(*token_pointer)) token_pointer++; // Skip spaces
      
      token = get_next_token(token_pointer);
    }

    if (token.kind == Declaration_Token_Kind::UNKNOWN_TOKEN)  return (Function_Declaration){};
    if (token.kind == Declaration_Token_Kind::CLOSING_PARENTHESE) {
      if (previous_token.kind & (Declaration_Token_Kind::TYPE | Declaration_Token_Kind::STAR | Declaration_Token_Kind::OPENING_PARENTHESE)) {
      } else {
        return (Function_Declaration){};
      }
    }
    if (token.kind == Declaration_Token_Kind::EOS) {
      if (previous_token.kind == Declaration_Token_Kind::CLOSING_PARENTHESE) {
      } else {
        return (Function_Declaration){};
      }
    }

  }

  return result;
}
