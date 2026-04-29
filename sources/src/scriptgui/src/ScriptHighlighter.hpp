#pragma once
#include "ScriptHighlighterBase.hpp"

/**
 * @brief Syntax highlighter for the uscript language.
 *
 * Inherits block-comment handling, highlightBlock(), fmt(), addRule(),
 * addMacroAssignRule(), addMacroVariableRule() and addTypedTokenDecorators()
 * from ScriptHighlighterBase.
 *
 * uscript-specific colour mapping (Dracula palette):
 *  ────────────────────────────────────────────────────────────────────
 *  NAME in NAME :=                  #ff79c6  pink   + bold    (base)
 *  := operator                      #ff79c6  pink             (base)
 *  NAME in NAME ?=                  #8be9fd  cyan             (here)
 *  NAME in NAME [=                  #8be9fd  cyan             (here)
 *  ?=  [=  operators                #ff79c6  pink             (here)
 *  $VAR / $ARR.$IDX                 #8be9fd  cyan             (base)
 *  PLUGIN.  namespace               #50fa7b  green  + bold    (here)
 *  .COMMAND                         #ffb86c  amber            (here)
 *  LOAD_PLUGIN / IF / GOTO / …      #ff79c6  pink   + bold    (here)
 *  PRINT / DELAY / MATH / …         #50fa7b  green            (here)
 *  BREAKPOINT                       #ff5555  red    + bold    (here)
 *  LABEL keyword                    #ff79c6  pink   + bold    (here)
 *  label name                       #f1fa8c  yellow           (here)
 *  :NUM  :STR  :VER  :BOOL          #8be9fd  cyan             (here)
 *  ==  !=  >=  <=  >  <             #ff79c6  pink             (here)
 *  AND  OR  NOT                     #ff79c6  pink             (here)
 *  H X R T L S F  prefix letter     (see base class table)   (base)
 *  All "..." content                #f1fa8c  yellow           (base)
 *  %0 %1 …  format tokens           #ffb86c  amber            (here)
 *  v1.2.3  version literals         #bd93f9  purple           (here)
 *  numeric literals                 #bd93f9  purple           (here)
 *  # comment / --- !-- delimiters   #6272a4  slate            (base)
 */
class ScriptHighlighter : public ScriptHighlighterBase
{
    Q_OBJECT
public:
    explicit ScriptHighlighter(QTextDocument *parent = nullptr);
};
