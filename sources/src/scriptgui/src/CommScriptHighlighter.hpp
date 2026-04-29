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
 *  Comm-specific colour mapping (Dracula palette):
 *  ────────────────────────────────────────────────────────────────────
 *  >  direction prefix              #ff5555  red    + bold    (here)
 *  <  direction prefix              #50fa7b  green  + bold    (here)
 *  !  delay prefix                  #ffb86c  amber  + bold    (here)
 *  delay number                     #bd93f9  purple           (here)
 *  delay unit (sec / ms / us)       #8be9fd  cyan             (here)
 *  |  pipe separator                #6272a4  slate            (here)
 *  NAME in NAME :=                  #ff79c6  pink   + bold    (base)
 *  := operator                      #ff79c6  pink             (base)
 *  $VAR / $ARR.$IDX                 #8be9fd  cyan             (base)
 *  H X R T L S F  prefix letter     (see base class table)   (base)
 *  All "..." content                #f1fa8c  yellow           (base)
 *  numeric literals                 #bd93f9  purple           (here)
 *  # comment / --- !-- delimiters   #6272a4  slate            (base)
 */
class CommScriptHighlighter : public ScriptHighlighterBase
{
    Q_OBJECT
public:
    explicit CommScriptHighlighter(QTextDocument *parent = nullptr);
};
