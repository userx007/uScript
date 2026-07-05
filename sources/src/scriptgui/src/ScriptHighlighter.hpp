#pragma once
#include "ScriptHighlighterBase.hpp"

/**
 * @brief Syntax highlighter for the uscript language.
 *
 * Inherits block-comment handling, highlightBlock(), fmt(), addRule(),
 * addMacroAssignRule(), addMacroVariableRule() and addTypedTokenDecorators()
 * from ScriptHighlighterBase.
 *
 *  Colour ownership — full table (Dracula-inspired palette):
 *  ──────────────────────────────────────────────────────────────────────
 *  Token / role                     Hex       Colour      Style   Owner
 *  ──────────────────────────────────────────────────────────────────────
 *  NAME in NAME :=                  #bd93f9   purple      bold    base
 *  := operator                      #ff79c6   pink                base
 *  NAME in NAME ?=                  #8be9fd   cyan        bold    here
 *  NAME in NAME [=                  #ffb86c   amber       bold    here
 *  ?=  [=  operators                #ff79c6   pink                here
 *    (all three assignment ops share pink for visual unity)
 *  $VAR / $ARR.$IDX                 #8be9fd   cyan                base
 *  $NAME  in $NAME.SIZE             #ffb86c   amber       bold    here
 *  SIZE   in $NAME.SIZE             #bd93f9   purple              here
 *    (SIZE reads a declared array macro's element count — see
 *     m_validateArraySizeUsage() in uScriptValidator.cpp)
 *  PLUGIN.  namespace               #20a39e   green       bold    here
 *  .COMMAND                         #ff5555   red         bold    here
 *    (green↔red complement pair — strongest contrast on the wheel)
 *  LOAD_PLUGIN keyword              #ff79c6   pink        bold    here
 *  LOAD_PLUGIN argument             #20a39e   green       bold    here
 *  IF / GOTO / REPEAT / UNTIL /     #ff79c6   pink        bold    here
 *    BREAK / CONTINUE / END_REPEAT
 *  LABEL keyword                    #ff79c6   pink        bold    here
 *    (same as all other control-flow keywords)
 *  label name                       #bd93f9   purple              here
 *    (same family as constant names)
 *  REPEAT range literal token       #89a1ef   blue                here
 *    (begin / end / step — decimal, hex, binary, octal, or float/exp)
 *  REPEAT range $macro token        #8be9fd   cyan                here
 *  REPEAT range ","  separator      #6272a4   slate               here
 *  PRINT / DELAY / FORMAT /         #a5b4fc   periwinkle  bold    here
 *    MATH / EVAL
 *  | HEX  post-processor pipe       #6272a4   slate               here
 *  HEX    post-processor keyword    #a5b4fc   periwinkle  bold    here
 *    (same as MATH/EVAL — HEX is a post-processing function)
 *  HEX width digits (8/16/.../128)  #bd93f9   purple              here
 *  HEX endian (LE / BE)             #8be9fd   cyan                here
 *  BREAKPOINT                       #ff5555   red         bold    here
 *  :NUM  :STR  :VER  :BOOL          #8be9fd   cyan                here
 *  ==  !=  >=  <=  >  <             #ff79c6   pink                here
 *  AND  OR  NOT                     #ff79c6   pink                here
 *  &  thread suffix                 #50fa7b   bright-green bold   here
 *    (Dracula "go" green — visually distinct, semantically: "launch")
 *  H  X  prefix letter              #ff5555   red         bold    base
 *  R  prefix letter                 #ffb86c   amber       bold    base
 *  T  L  prefix letter              #8be9fd   cyan        bold    base
 *  S  prefix letter                 #bd93f9   purple      bold    base
 *  F  prefix letter                 #ff79c6   pink        bold    base
 *  All "..." content                #f1fa8c   yellow              base  (RESERVED)
 *  %0 %1 …  format tokens           #ffb86c   amber               here
 *  v1.2.3  version literals         #89a1ef   blue                here
 *  numeric literals                 #89a1ef   blue                here
 *    (hex 0x… / binary 0b… / octal 0o… / decimal, optionally signed —
 *     see ScriptHighlighterBase::addNumericLiteralRule())
 *  # comment / --- !-- delimiters   #6272a4   slate               base
 *  ──────────────────────────────────────────────────────────────────────
 */
class ScriptHighlighter : public ScriptHighlighterBase
{
    Q_OBJECT
public:
    explicit ScriptHighlighter(QTextDocument *parent = nullptr);
};
