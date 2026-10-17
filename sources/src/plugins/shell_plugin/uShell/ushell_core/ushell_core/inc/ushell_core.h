/*
MIT License Copyright (c) 2022, Victor Marian Popa (victormarianpopa@gmail.com)
*/

#ifndef USHELL_CORE_H
#define USHELL_CORE_H

#include "ushell_core_datatypes.h"
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
#include <memory>
#endif /* (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES) */

#define uSHELL_VERSION "1.0.0"

/*==============================================================================
            MICROSHELL CLASS DEFINITION
==============================================================================*/

class Microshell
{
    public:
        /** Returns the shell instance bound to @p psShellInst, creating it on first use; @p pstrPromptExt is appended to the prompt. */
        static Microshell *getShellPtr(uShellInst_s *psShellInst, const char *pstrPromptExt);
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
        static std::shared_ptr<Microshell> getShellSharedPtr(uShellInst_s *psShellInst, const char *pstrPromptExt);
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/
        /** Prints the prompt and processes key presses until the shell exits. */
        void Run(void);
#if (1 == uSHELL_SUPPORTS_COMMAND_AS_PARAMETER)
        /** Runs @p pstrCommand as if it had been typed; returns false if it could not be parsed or executed. */
        bool Execute(const char *pstrCommand);
#endif /* (1 == uSHELL_SUPPORTS_COMMAND_AS_PARAMETER) */

    private:
        Microshell(uShellInst_s *pShellInst, const char *pstrPromptExt);
        /* shell core private functions */
        static void m_Init(const char *pstrPromptExt);
        /** Processes one key press; returns false once the shell has been asked to exit. */
        static bool m_Execute(void);
        /** Rebuilds the prompt string with @p pstrPromptExt. */
        static void m_CoreSetPrompt(const char *pstrPromptExt);
        /** Handles Enter: runs a shortcut, or records the line in history and executes it as a command. */
        static void m_CoreExecuteEnterKey(void);
        /** Tokenises the input line into m_sCommand (name and arguments); returns uSHELL_ERR_OK or an error code. */
        static int m_CoreParseCommand(void);
        /** Parses and executes the input line, printing the result or the error. */
        static void m_CoreParseExecuteCommand(void);
        /** Returns the index of the command named @p pstrFctName, or uSHELL_ERR_FUNCTION_NOT_FOUND. */
        static int m_CoreSearchFunction(const char *pstrFctName);
        /** Prints the message for error code @p iError. */
        static void m_CorePrintError(const int iError);
        /** Writes @p pstrArray to the terminal. */
        static void m_CorePutString(const char *pstrArray);
        static void m_CoreProcessKeyPress(const char cKeyPressed);
        /** Clears the input buffer and cursor; @p bFull also resets the edit mode. */
        static void m_CoreResetInput(const bool bFull);
        /** Strips trailing spaces from the input buffer. */
        static void m_CoreRemoveTrailingSpaces(void);
        /** Prints a "<feature> <status>" notice, e.g. "history on". */
        static void m_CorePrintMessage(const int iFeatIdx, const int iStatusIdx);
        /** Prints the prompt. */
        static void m_CorePrintPrompt(void);

#if (1 == uSHELL_IMPLEMENTS_SHELL_EXIT)
        /** Requests the shell loop to stop. */
        static void m_CoreExit(void);
#endif /*(1 == uSHELL_IMPLEMENTS_SHELL_EXIT)*/

#if (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)
        static void m_CoreShowInfo(const char *pstrArgs);
        static void m_CoreShowCmdInfo(const int iFctIndex, const bool bParamInfo);
        /** Lists the core and user shortcuts. */
        static void m_CoreShowShortcuts(void);
        /** Lists the supported parameter data types. */
        static void m_CoreShowTypes(void);
        static void m_CorePutChars(const char *pstrArray, int iNrChars, const bool bNewLine);
#endif /* (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)*/
        /** Prints one command-list line: index, name and parameter signature. */
        static void m_CoreShowCmd(int iFctIndex);
        /** Lists all registered commands. */
        static void m_CoreShowCmdsList(void);

#if defined(uSHELL_IMPLEMENTS_STRINGS)
#if (1 == uSHELL_SUPPORTS_SPACED_STRINGS)
        /** Extracts the next argument, treating text between string-border symbols as one token. */
        static int m_CoreHandleBorderedStrings(char **ppstrPpstrToken, char **ppstrPpstrRest, int *pIntArgCounter);
        /** Sets the string-border symbol (a single character; empty restores the quotation mark). */
        static void m_CoreSetStringBorder(const char *pstrStringBorder);
#endif /*(1 == uSHELL_SUPPORTS_SPACED_STRINGS)*/
#endif /*defined(uSHELL_IMPLEMENTS_STRINGS)*/

        /* core key handlers */
        static void m_CoreHandleKeyEnter(void);
        /** Handles a printable key by inserting it into the input line. */
        static void m_CoreHandleKeyDefault(const char cKeyPressed);
        /** Runs the shortcut whose symbol starts the input line; returns false if there is none. */
        static bool m_CoreHandleShortcuts(void);
        /** True if @p cKey is a punctuation character usable as a shortcut symbol. */
        static bool m_CoreIsShortcutSymbol(const char cKey);
        /** Built-in `#` shortcut: core commands such as help, history and autocomplete control (selected by @p pstrArgs). */
        static void m_CoreHandleShortcut_Hash(const char *pstrArgs);

#if (1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_HISTORY)
        /** Up/down arrow: navigates history or autocomplete candidates. */
        static void m_CoreHandleKeyArrowUpDown(const dir_e eDir);
#endif /*(1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_HISTORY)*/

#if (1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
        /** Left/right arrow: moves the cursor in edit mode. */
        static void m_CoreHandleKeyArrowLeftRight(const dir_e eDir);
#endif /*(1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)*/

        /** Decodes an ANSI escape sequence (arrows, Home/End, Delete, Insert) and dispatches it. */
        static void m_CoreHandleKeyEscapeSeq(void);
        /** Deletes the character before the cursor. */
        static void m_CoreHandleKeyBackspace(void);
        /** Deletes the character under the cursor (edit mode) or clears the line. */
        static void m_CoreHandleKeyDelete(void);
        /** Erases the command line on screen and resets the input. */
        static void m_CoreCmdLineDelete(void);

#if (1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)
        static bool m_CoreConfirmRequest(void);
#endif /*(1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)*/

#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
        /** Moves the edit cursor in direction @p eDir; false if edit mode is off or the move is not possible. */
        static bool m_EditMoveCursor(const dir_e eDir);
        static void m_EditMoveCursorDirSteps(const dir_e eDir, const int iSteps);
        /** Inserts @p cKeyPressed at the cursor, shifting the rest of the line right. */
        static void m_EditInsertUnderCursor(const char cKeyPressed);
        /** Deletes the character at the cursor, shifting the rest of the line left. */
        static void m_EditDeleteUnderCursor(void);
        /** Deletes the character before the cursor and moves the cursor back. */
        static void m_EditDeleteBackward(void);
        /** Deletes from the line start up to the cursor. */
        static void m_EditDeleteBackwardToHome(void);
        /** Deletes from the cursor to the line end. */
        static void m_EditDeleteForwardToEnd(void);
#if !defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)
        /** Insert key: toggles edit mode. */
        static void m_CoreHandleKeyInsert(void);
#endif /* !defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE) */
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */

#if (1 == uSHELL_IMPLEMENTS_HISTORY)
        /* history wrapper functions */
        static void m_HistoryInit(const char *pstrFileName);
        /** Clears the history and marks it uninitialised. */
        static void m_HistoryDeInit(void);
        /** Stores the current input line in history (if history is enabled). */
        static void m_HistoryWrite(void);
        /** Clears the history after confirmation. */
        static void m_HistoryReset(void);
        /** Prints the numbered history entries. */
        static void m_HistoryList(void);
        /** Re-runs the history entry whose index is given in @p pstrIndex. */
        static void m_HistoryExecuteEntry(const char *pstrIndex);
        /** Loads the previous or next history entry (per @p eDir) into the input line. */
        static void m_HistoryRead(const dir_e eDir);
        /** Loads entry @p iIndex into the input buffer and returns it; nullptr if unavailable. */
        static char *m_HistoryGetEntry(int iIndex);
        /** Enables or disables history. */
        static void m_HistoryEnable(const bool bEnable);

        /* Embedded history implementation functions */
        static void m_HistoryInitCore(history_s *pHistory, char *pstrDataBuffer, size_t szCapacity);
        /** Appends the trimmed input line to the history ring buffer, evicting the oldest entries if needed; false if the line is empty or too long. */
        static bool m_HistoryPush(history_s *pHistory, bool bTriggerAutosave);
        /** Steps the history cursor back (wrapping) and copies that entry to @p pstrBuffer. */
        static bool m_HistoryGetPrevEntry(history_s *pHistory, char *pstrBuffer, size_t szBufferSize);
        /** Steps the history cursor forward (wrapping) and copies that entry to @p pstrBuffer. */
        static bool m_HistoryGetNextEntry(history_s *pHistory, char *pstrBuffer, size_t szBufferSize);
        /** Copies the oldest entry to @p pstrBuffer. */
        static bool m_HistoryGetFirstEntry(const history_s *pHistory, char *pstrBuffer, size_t szBufferSize);
        /** Copies the newest entry to @p pstrBuffer. */
        static bool m_HistoryGetLastEntry(const history_s *pHistory, char *pstrBuffer, size_t szBufferSize);
        /** Sets the history cursor to @p szIndex if it is in range. */
        static void m_HistorySetIndex(history_s *pHistory, size_t szIndex);
        /** True if the history holds no entries. */
        static bool m_HistoryIsEmpty(const history_s *pHistory);
        /** Copies entry @p szIndex (0 = oldest) to @p pstrBuffer; false if out of range. */
        static bool m_HistoryGetEntryAtIndex(const history_s *pHistory, size_t szIndex, char *pstrBuffer, size_t szBufferSize);
        /** Removes all entries and zeroes the buffer. */
        static void m_HistoryClear(history_s *pHistory);
        /** Returns the unused buffer bytes in @p pPszFreeBytes. */
        static void m_HistoryGetFreeSpace(const history_s *pHistory, size_t *pPszFreeBytes);
        /** Returns the number of stored entries. */
        static size_t m_HistoryGetEntrySize(const history_s *pHistory);
        /** Starts an iteration at the oldest entry. */
        static void m_HistoryIteratorInit(historyIter_s *pIter, const history_s *pHistory);
        /** Copies the next entry to @p pstrBuffer and advances; false at the end. */
        static bool m_HistoryIteratorNext(historyIter_s *pIter, char *pstrBuffer, size_t szBufferSize);
        /** Prints all entries, oldest first. */
        static void m_HistoryShow(const history_s *pHistory);

        /* Helpers */
        static void m_HistoryWriteLengthAt(char *pstrBuffer, size_t szCapacity, size_t szPos, uint16_t u16Len);
        /** Reads the 16-bit big-endian entry length stored at @p szPos (wrapping in the ring buffer). */
        static uint16_t m_HistoryReadLengthAt(const char *pstrBuffer, size_t szCapacity, size_t szPos);
        /** Returns the stored size of an entry: metadata plus @p u16DataLen bytes. */
        static inline size_t m_HistoryEntryTotalSize(uint16_t u16DataLen);
        /** Returns the buffer position of the entry following the one at @p szPos. */
        static size_t m_HistoryFindNextEntryPos(const history_s *pHistory, size_t szPos);
        /** Returns the number of buffer bytes occupied by entries. */
        static size_t m_HistoryCalculateUsedSpace(const history_s *pHistory);
        /** Drops the oldest entry. */
        static void m_HistoryRemoveOldestEntry(history_s *pHistory);
#endif /* (1 == uSHELL_IMPLEMENTS_HISTORY) */

#if ((1 == uSHELL_IMPLEMENTS_HISTORY) && (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY))
        /** Reloads the history from its file. */
        static void m_HistoryReload(void);
        /** Sets the file used to persist history. */
        static void m_HistorySetFilePath(history_s *pHistory, const char *pstrFilePath);
        /** Loads history entries from the configured file; false if it cannot be read. */
        static bool m_HistoryLoadFromFile(history_s *pHistory);
        /** Enables or disables appending new entries to the history file. */
        static void m_HistoryEnableAutoSave(history_s *pHistory, bool bEnable);
        /** Appends @p pstrEntry to the history file; false on failure. */
        static bool m_HistoryAppendToFile(history_s *pHistory, const char *pstrEntry);
        /** Sets the history file to `.hist_<pstrFileName>` and enables auto-save. */
        static void m_HistoryInitFile(const char *pstrFileName);
#endif /*((1 == uSHELL_IMPLEMENTS_HISTORY) && (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY))*/

        /* autocomplete functions */
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
        static void m_AutocomplInit(void);
        /** Fills the candidate list with all commands (@p bFull) or the commands matching the input. */
        static void m_AutocomplFill(const bool bFull);
        /** Forces the next filter pass to start from the full command list. */
        static void m_AutocomplReInit(void);
        /** Clears the candidate list and search state; @p bReinit restarts filtering. */
        static void m_AutocomplReset(const bool bReinit);
        /** Extends the input with the prefix common to all current candidates. */
        static void m_AutocomplGetCommon(void);
        /** Narrows the candidates to commands starting with the current input. */
        static void m_AutocomplFilter(void);
        /** Appends a space after an exact command match. */
        static void m_AutocomplInsEndSpace(void);
        /** Cycles through the candidates in direction @p eDir and shows the selection. */
        static void m_AutocomplRead(const dir_e eDir);
        /** Enables or disables autocomplete. */
        static void m_AutocomplEnable(const bool bEnable);
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */

#if (1 == uSHELL_IMPLEMENTS_KEY_DECODER)
        /** Debug loop that prints the codes of pressed keys until `:exit:`. */
        static void keydecoder(void);
#endif /*(1 == uSHELL_IMPLEMENTS_KEY_DECODER)*/

        static char m_pstrInput[uSHELL_MAX_INPUT_BUF_LEN];
        static int m_iInputPos;
        static int m_iCursorPos;
        static command_s m_sCommand;

#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
        static autocomplete_s m_sAutocomplete;
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */

#if (1 == uSHELL_IMPLEMENTS_HISTORY)
        /* Embedded history implementation */
        static history_s m_sHistory;
        static char m_historyBuffer[uSHELL_HISTORY_BUFFER_SIZE];
        static bool m_bHistoryEnabled;
        static bool m_bHistoryInitialized;
#if (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY)
        static char m_HistoryFilePath[uSHELL_HISTORY_FILEPATH_LENGTH];
#endif
#endif /* (1 == uSHELL_IMPLEMENTS_HISTORY) */

#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
        static bool m_bEditMode;
#endif /*(1 == uSHELL_IMPLEMENTS_EDITMODE) */

#if (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO)
        static bool m_bEchoOn;
#endif /* (1 == uSHELL_IMPLEMENTS_DISABLE_ECHO) */

        static const char *m_pstrCoreShortcutCaption;
#if (defined(uSHELL_IMPLEMENTS_STRINGS) && (1 == uSHELL_SUPPORTS_SPACED_STRINGS))
        static char m_cStringBorderSymbol;
#endif /*(defined(uSHELL_IMPLEMENTS_STRINGS) && (1 == uSHELL_SUPPORTS_SPACED_STRINGS))*/

        /* delimiters/separators */
        static constexpr const char *m_pstrTokenSeparator = " ";

        /* data types related */
#define uSHELL_DATA_TYPES_TABLE_BEGIN \
    enum typemarks_e                  \
    {
#define uSHELL_DATA_TYPE(a, b) uSHELL_TYPE_##a,
#define uSHELL_DATA_TYPES_TABLE_END \
    uSHELL_TYPE_LAST                \
    }                               \
    ;
#include uSHELL_DATA_TYPES_CONFIG_FILE
#undef uSHELL_DATA_TYPES_TABLE_BEGIN
#undef uSHELL_DATA_TYPE
#undef uSHELL_DATA_TYPES_TABLE_END

#if (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)
        static const char m_vstrTypeMarks[uSHELL_TYPE_LAST];
#endif /*(1 == uSHELL_IMPLEMENTS_COMMAND_HELP)*/
        static const char *m_vstrTypeNames[uSHELL_TYPE_LAST];

        /* prompt related */
#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
#define uSHELL_PROMPT_TABLE_BEGIN \
    enum prompti_e                \
    {
#define uSHELL_PROMPT_CELL(a, b, c) uSHELL_PROMPTI_##a,
#define uSHELL_PROMPT_TABLE_END \
    uSHELL_PROMPTI_LAST         \
    }                           \
    ;
#include uSHELL_PROMPT_CONFIG_FILE
#undef uSHELL_PROMPT_TABLE_BEGIN
#undef uSHELL_PROMPT_CELL
#undef uSHELL_PROMPT_TABLE_END

        static char m_pstrPrompt[uSHELL_PROMPTI_LAST + 1];
        static const char m_pstrPromptInfo[uSHELL_PROMPTI_LAST + 1];
        static const char m_pstrPromptInfoEditMode[uSHELL_PROMPTI_LAST + 1];
        /** Marks prompt flag @p ePromptIndex as on (upper case) or off (lower case). */
        static void m_CoreUpdatePrompt(const prompti_e ePromptIndex, const bool bOnOff);
#endif /*(1 == uSHELL_IMPLEMENTS_SMART_PROMPT)*/

        static uShellInst_s *m_pInst;
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
        static int m_iInstanceCounter;
        static uShellInst_s *m_pInstBackup;
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/
};

#endif /* USHELL_CORE_H */
