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

class Microshell {
  public:
    static Microshell *getShellPtr(uShellInst_s *psShellInst, const char *pstrPromptExt);
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
    static std::shared_ptr<Microshell> getShellSharedPtr(uShellInst_s *psShellInst, const char *pstrPromptExt);
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/
    void Run(void);
#if (1 == uSHELL_SUPPORTS_COMMAND_AS_PARAMETER)
    bool Execute(const char *pstrCommand);
#endif /* (1 == uSHELL_SUPPORTS_COMMAND_AS_PARAMETER) */

  private:
    Microshell(uShellInst_s *psShellInst, const char *pstrPromptExt);
    /* shell core private functions */
    static void m_Init(const char *pstrPromptExt);
    static bool m_Execute(void);
    static void m_CoreSetPrompt(const char *pstrPromptExt);
    static void m_CoreExecuteEnterKey(void);
    static int m_CoreParseCommand(void);
    static void m_CoreParseExecuteCommand(void);
    static int m_CoreSearchFunction(const char *pstrFctName);
    static void m_CorePrintError(const int iError);
    static void m_CorePutString(const char *pstrArray);
    static void m_CoreProcessKeyPress(const char cKeyPressed);
    static void m_CoreResetInput(const bool bFull);
    static void m_CoreRemoveTrailingSpaces(void);
    static void m_CorePrintMessage(const int iFeatIdx, const int iStatusIdx);
    static void m_CorePrintPrompt(void);

#if (1 == uSHELL_IMPLEMENTS_SHELL_EXIT)
    static void m_CoreExit(void);
#endif /*(1 == uSHELL_IMPLEMENTS_SHELL_EXIT)*/

#if (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)
    static void m_CoreShowInfo(const char *pstrArgs);
    static void m_CoreShowCmdInfo(const int iFctIndex, const bool bParamInfo);
    static void m_CoreShowShortcuts(void);
    static void m_CoreShowTypes(void);
    static void m_CorePutChars(const char *pstrArray, int iNrChars, const bool bNewLine);
#endif /* (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)*/
    static void m_CoreShowCmd(int iFctIndex);
    static void m_CoreShowCmdsList(void);

#if defined(uSHELL_IMPLEMENTS_STRINGS)
#if (1 == uSHELL_SUPPORTS_SPACED_STRINGS)
    static int m_CoreHandleBorderedStrings(char **ppstrToken, char **ppstrRest, int *pIntArgCounter);
    static void m_CoreSetStringBorder(const char *pstrStringBorder);
#endif /*(1 == uSHELL_SUPPORTS_SPACED_STRINGS)*/
#endif /*defined(uSHELL_IMPLEMENTS_STRINGS)*/

    /* core key handlers */
    static void m_CoreHandleKeyEnter(void);
    static void m_CoreHandleKeyDefault(const char cKeyPressed);
    static bool m_CoreHandleShortcuts(void);
    static bool m_CoreIsShortcutSymbol(const char cKey);
    static void m_CoreHandleShortcut_Hash(const char *pstrArgs);

#if (1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_HISTORY)
    static void m_CoreHandleKeyArrowUpDown(const dir_e eDir);
#endif /*(1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_HISTORY)*/

#if (1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
    static void m_CoreHandleKeyArrowLeftRight(const dir_e eDir);
#endif /*(1 == uSHELL_IMPLEMENTS_EDITMODE) || (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)*/

    static void m_CoreHandleKeyEscapeSeq(void);
    static void m_CoreHandleKeyBackspace(void);
    static void m_CoreHandleKeyDelete(void);
    static void m_CoreCmdLineDelete(void);

#if (1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)
    static bool m_CoreConfirmRequest(void);
#endif /*(1 == uSHELL_IMPLEMENTS_CONFIRM_REQUEST)*/

#if (1 == uSHELL_IMPLEMENTS_EDITMODE)
    static bool m_EditMoveCursor(const dir_e eDir);
    static void m_EditMoveCursorDirSteps(const dir_e eDir, const int iSteps);
    static void m_EditInsertUnderCursor(const char cKeyPressed);
    static void m_EditDeleteUnderCursor(void);
    static void m_EditDeleteBackward(void);
    static void m_EditDeleteBackwardToHome(void);
    static void m_EditDeleteForwardToEnd(void);
#if !defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE)
    static void m_CoreHandleKeyInsert(void);
#endif /* !defined(uSHELL_EDIT_MODE_DEFAULT_ACTIVE) */
#endif /* (1 == uSHELL_IMPLEMENTS_EDITMODE) */

#if (1 == uSHELL_IMPLEMENTS_HISTORY)
    /* history wrapper functions */
    static void m_HistoryInit(const char *pstrFileName);
    static void m_HistoryDeInit(void);
    static void m_HistoryWrite(void);
    static void m_HistoryReset(void);
    static void m_HistoryList(void);
    static void m_HistoryExecuteEntry(const char *pstrIndex);
    static void m_HistoryRead(const dir_e eDir);
    static char *m_HistoryGetEntry(int iIndex);
    static void m_HistoryEnable(const bool bEnable);

    /* Embedded history implementation functions */
    static void m_HistoryInitCore(history_s *pHistory, char *pDataBuffer, size_t szCapacity);
    static bool m_HistoryPush(history_s *pHistory, bool bTriggerAutosave);
    static bool m_HistoryGetPrevEntry(history_s *pHistory, char *pBuffer, size_t szBufferSize);
    static bool m_HistoryGetNextEntry(history_s *pHistory, char *pBuffer, size_t szBufferSize);
    static bool m_HistoryGetFirstEntry(const history_s *pHistory, char *pBuffer, size_t szBufferSize);
    static bool m_HistoryGetLastEntry(const history_s *pHistory, char *pBuffer, size_t szBufferSize);
    static void m_HistorySetIndex(history_s *pHistory, size_t szIndex);
    static bool m_HistoryIsEmpty(const history_s *pHistory);
    static bool m_HistoryGetEntryAtIndex(const history_s *pHistory, size_t szIndex, char *pBuffer, size_t szBufferSize);
    static void m_HistoryClear(history_s *pHistory);
    static void m_HistoryGetFreeSpace(const history_s *pHistory, size_t *pszFreeBytes);
    static size_t m_HistoryGetEntrySize(const history_s *pHistory);
    static void m_HistoryIteratorInit(historyIter_s *pIter, const history_s *pHistory);
    static bool m_HistoryIteratorNext(historyIter_s *pIter, char *pBuffer, size_t szBufferSize);
    static void m_HistoryShow(const history_s *pHistory);

    /* Helpers */
    static void m_HistoryWriteLengthAt(char *pBuffer, size_t szCapacity, size_t szPos, uint16_t u16Len);
    static uint16_t m_HistoryReadLengthAt(const char *pBuffer, size_t szCapacity, size_t szPos);
    static inline size_t m_HistoryEntryTotalSize(uint16_t u16DataLen);
    static size_t m_HistoryFindNextEntryPos(const history_s *pHistory, size_t szPos);
    static size_t m_HistoryCalculateUsedSpace(const history_s *pHistory);
    static void m_HistoryRemoveOldestEntry(history_s *pHistory);
#endif /* (1 == uSHELL_IMPLEMENTS_HISTORY) */

#if ((1 == uSHELL_IMPLEMENTS_HISTORY) && (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY))
    static void m_HistoryReload(void);
    static void m_HistorySetFilePath(history_s *pHistory, const char *pstrFilePath);
    static bool m_HistoryLoadFromFile(history_s *pHistory);
    static void m_HistoryEnableAutoSave(history_s *pHistory, bool bEnable);
    static bool m_HistoryAppendToFile(history_s *pHistory, const char *pstrEntry);
    static void m_HistoryInitFile(const char *pstrFileName);
#endif /*((1 == uSHELL_IMPLEMENTS_HISTORY) && (1 == uSHELL_IMPLEMENTS_SAVE_HISTORY))*/

    /* autocomplete functions */
#if (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE)
    static void m_AutocomplInit(void);
    static void m_AutocomplFill(const bool bFull);
    static void m_AutocomplReInit(void);
    static void m_AutocomplReset(const bool bReinit);
    static void m_AutocomplGetCommon(void);
    static void m_AutocomplFilter(void);
    static void m_AutocomplInsEndSpace(void);
    static void m_AutocomplRead(const dir_e eDir);
    static void m_AutocomplEnable(const bool bEnable);
#endif /* (1 == uSHELL_IMPLEMENTS_AUTOCOMPLETE) */

#if (1 == uSHELL_IMPLEMENTS_KEY_DECODER)
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
#define  uSHELL_DATA_TYPES_TABLE_BEGIN      enum typemarks_e {
#define  uSHELL_DATA_TYPE(a, b)                 uSHELL_TYPE_##a,
#define  uSHELL_DATA_TYPES_TABLE_END        uSHELL_TYPE_LAST };
#include uSHELL_DATA_TYPES_CONFIG_FILE
#undef   uSHELL_DATA_TYPES_TABLE_BEGIN
#undef   uSHELL_DATA_TYPE
#undef   uSHELL_DATA_TYPES_TABLE_END

#if (1 == uSHELL_IMPLEMENTS_COMMAND_HELP)
    static const char m_vstrTypeMarks[uSHELL_TYPE_LAST];
#endif /*(1 == uSHELL_IMPLEMENTS_COMMAND_HELP)*/
    static const char *m_vstrTypeNames[uSHELL_TYPE_LAST];

    /* prompt related */
#if (1 == uSHELL_IMPLEMENTS_SMART_PROMPT)
#define  uSHELL_PROMPT_TABLE_BEGIN      enum prompti_e {
#define  uSHELL_PROMPT_CELL(a, b, c)        uSHELL_PROMPTI_##a,
#define  uSHELL_PROMPT_TABLE_END        uSHELL_PROMPTI_LAST };
#include uSHELL_PROMPT_CONFIG_FILE
#undef   uSHELL_PROMPT_TABLE_BEGIN
#undef   uSHELL_PROMPT_CELL
#undef   uSHELL_PROMPT_TABLE_END

    static char m_pstrPrompt[uSHELL_PROMPTI_LAST + 1];
    static const char m_pstrPromptInfo[uSHELL_PROMPTI_LAST + 1];
    static const char m_pstrPromptInfoEditMode[uSHELL_PROMPTI_LAST + 1];
    static void m_CoreUpdatePrompt(const prompti_e ePromptIndex, const bool bOnOff);
#endif /*(1 == uSHELL_IMPLEMENTS_SMART_PROMPT)*/

    static uShellInst_s *m_pInst;
#if (1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)
    static int m_iInstanceCounter;
    static uShellInst_s *m_pInstBackup;
#endif /*(1 == uSHELL_SUPPORTS_MULTIPLE_INSTANCES)*/
};

#endif /* USHELL_CORE_H */
