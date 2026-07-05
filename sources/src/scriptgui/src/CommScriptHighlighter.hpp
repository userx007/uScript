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
 *    > EXPR1 | EXPR2               SEND_RECV  (send EXPR1, optionally match EXPR2)
 *    < EXPR1 | EXPR2               RECV_SEND  (receive EXPR1, optionally send EXPR2)
 *    ! <number> <unit>             DELAY      (delay: sec / ms / us)
 *    @ <message>                   PRINT      (log <message> at INFO severity, no I/O)
 *    NAME := value                 macro definition  (same as main script)
 *    # ...                         line comment      (same as main script)
 *    ---  …  !--                   block comment     (same as main script)
 *
 *  xtra_params extension (appended after EXPR1 / EXPR2):
 *    > EXPR1 ~ param               param forwarded to tout_write as xtra_params
 *    < EXPR1 ~ param               param forwarded to tout_read  as xtra_params
 *    > EXPR1 | EXPR2 ~ param       param forwarded to both operations
 *    > EXPR1 | EXPR2 ~ p1 / p2     p1 → first op, p2 → second op
 *                                  (/ only valid when | is present)
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
 *  @  print prefix                  #a5b4fc   periwinkle bold  here
 *  print message body               #a5b4fc   periwinkle italic here
 *    (same hue as the bold prefix - italic marks the "quieter" content role,
 *     same relationship as m_commentFmt/m_delimFmt in the base class; also
 *     the same colour family as core script's PRINT native function)
 *  |  pipe separator                #6272a4   slate            here
 *  ~  xtra_params separator         #ffb86c   amber    bold    here
 *    (amber — same family as ! modifier sigil)
 *  xtra_param values                #ff79c6   pink             here
 *    (pink — same family as := and F prefix; all denote addressing/resources)
 *  /  per-op param separator        #6272a4   slate            here
 *    (slate — same as |; both are structural separators)
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
