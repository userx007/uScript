#pragma once
#include "ScriptHighlighterBase.hpp"

/**
 * @brief Syntax highlighter for comm script files.
 *
 * Inherits block-comment handling, highlightBlock(), fmt(), addRule(),
 * addMacroAssignRule(), addMacroVariableRule() and addTypedTokenDecorators()
 * from ScriptHighlighterBase.
 *
 * Comm script syntax (from uCommScriptCommandValidator / uCommScriptDataTypes):
 *
 *  Line structure:
 *    > EXPR1 | EXPR2     SEND_RECV  (send EXPR1, optionally match EXPR2)
 *    < EXPR1 | EXPR2     RECV_SEND  (receive EXPR1, optionally send EXPR2)
 *    ! <number> <unit>   DELAY      (delay: sec / ms / us)
 *    NAME := value       macro definition  (same as main script)
 *    # ...               line comment      (same as main script)
 *    ---  …  !--         block comment     (same as main script)
 *
 *  Token decorators (prefix + quoted content) — rendered by base:
 *    H"hex"      HEXSTREAM        hex byte sequence
 *    R"pattern"  REGEX            regular expression
 *    T"str"      TOKEN_STRING     string token to wait for
 *    X"hex"      TOKEN_HEXSTREAM  hex token to wait for
 *    L"str"      LINE             line-terminated read
 *    S"num"      SIZEOF           byte count
 *    F"file"     FILENAME         binary file path
 *    "str"       STRING_DELIMITED plain quoted string
 *    word        STRING_RAW       unquoted string
 *
 *  Colour ownership — full table (Dracula-inspired palette):
 *  ──────────────────────────────────────────────────────────────────────
 *  Token / role                     Hex       Colour   Style   Owner
 *  ──────────────────────────────────────────────────────────────────────
 *  >  send direction prefix         #ff5555   red      bold    here
 *  <  recv direction prefix         #50fa7b   green    bold    here
 *    (red↔green complement pair — strongest contrast on the wheel)
 *  !  delay prefix                  #ffb86c   amber    bold    here
 *  delay number                     #bd93f9   purple           here
 *  delay unit (sec / ms / us)       #8be9fd   cyan             here
 *    (warm › cool triad: amber ! → purple N → cyan unit)
 *  |  pipe separator                #6272a4   slate            here
 *  NAME in NAME :=                  #bd93f9   purple   bold    base
 *  := operator                      #ff79c6   pink             base
 *  $VAR / $ARR.$IDX                 #8be9fd   cyan             base
 *  H  X  prefix letter              #ff5555   red      bold    base  (raw bytes)
 *  R  prefix letter                 #ffb86c   amber    bold    base  (pattern)
 *  T  L  prefix letter              #8be9fd   cyan     bold    base  (stream tokens)
 *  S  prefix letter                 #bd93f9   purple   bold    base  (numeric size)
 *  F  prefix letter                 #ff79c6   pink     bold    base  (file resource)
 *  All "..." content                #f1fa8c   yellow           base  (RESERVED)
 *  numeric literals                 #bd93f9   purple           here
 *  # comment / --- !-- delimiters   #6272a4   slate            base
 *  ──────────────────────────────────────────────────────────────────────
 */
class CommScriptHighlighter : public ScriptHighlighterBase
{
    Q_OBJECT
public:
    explicit CommScriptHighlighter(QTextDocument *parent = nullptr);
};
