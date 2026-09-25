#pragma once
// Differential audit of the codegen fork's locals form (docs/codegen-fork.md).
//
// Appended after the generated edf2017_pch.h to the recompiled-code target only when
// EDF_CODEGEN_FORK_AUDIT is ON (CMakeLists.txt). Every guest scalar store first tells
// the current thread's journal (if an audit is replaying a call) which host bytes it is
// about to change, so the auditor can roll the first run back and compare both runs.
// Replayable bodies never use MMIO, raw (dcbz/stvx) or atomic stores; the codegen
// fork leaves those functions out of the audit.

#include <cstdint>

struct EdfIpaJournal;
extern thread_local EdfIpaJournal* edf_ipa_tl_journal;
void edf_ipa_journal_record(EdfIpaJournal* journal, uint8_t* host, unsigned size);

#define EDF_IPA_HOST(x) (base + (u32)(x) + REX_PHYS_HOST_OFFSET(x))
#define EDF_IPA_NOTE(x, n) \
  (edf_ipa_tl_journal ? edf_ipa_journal_record(edf_ipa_tl_journal, EDF_IPA_HOST(x), (n)) : (void)0)

#undef REX_STORE_U8
#undef REX_STORE_U16
#undef REX_STORE_U32
#undef REX_STORE_U64
#define REX_STORE_U8(x, y) (EDF_IPA_NOTE(x, 1), (*(volatile u8*)EDF_IPA_HOST(x) = (y)))
#define REX_STORE_U16(x, y) \
  (EDF_IPA_NOTE(x, 2), (*(volatile u16*)EDF_IPA_HOST(x) = __builtin_bswap16(y)))
#define REX_STORE_U32(x, y) \
  (EDF_IPA_NOTE(x, 4), (*(volatile u32*)EDF_IPA_HOST(x) = __builtin_bswap32(y)))
#define REX_STORE_U64(x, y) \
  (EDF_IPA_NOTE(x, 8), (*(volatile u64*)EDF_IPA_HOST(x) = __builtin_bswap64(y)))
