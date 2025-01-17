/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Assembler.h"

#include "ProcessRecordReplay.h"

#include <sys/mman.h>

namespace mozilla {
namespace recordreplay {

Assembler::Assembler(uint8_t* aStorage, size_t aSize)
    : mCursor(aStorage),
      mCursorEnd(aStorage + aSize) {}

// The maximum byte length of an x86/x64 instruction.
static const size_t MaximumInstructionLength = 15;

void Assembler::Advance(size_t aSize) {
  mCursor += aSize;
  MOZ_RELEASE_ASSERT(mCursor + MaximumInstructionLength <= mCursorEnd);
}

uint8_t* Assembler::Current() {
  return mCursor;
}

void Assembler::Jump(void* aTarget) {
  PushImmediate(aTarget);
  Return();
}

void Assembler::PushImmediate(void* aValue) {
  // Push the target literal onto the stack, 2 bytes at a time. This is
  // apparently the best way of getting an arbitrary 8 byte literal onto the
  // stack, as 4 byte literals we push will be sign extended to 8 bytes.
  size_t nvalue = reinterpret_cast<size_t>(aValue);
  Push16(nvalue >> 48);
  Push16(nvalue >> 32);
  Push16(nvalue >> 16);
  Push16(nvalue);
}

void Assembler::Push16(uint16_t aValue) {
  uint8_t* ip = Current();
  ip[0] = 0x66;
  ip[1] = 0x68;
  *reinterpret_cast<uint16_t*>(ip + 2) = aValue;
  Advance(4);
}

void Assembler::Return() {
  NewInstruction(0xC3);
}

void Assembler::Breakpoint() {
  NewInstruction(0xCC);
}

void Assembler::PushRax() { NewInstruction(0x50); }

void Assembler::PopRax() { NewInstruction(0x58); }

void Assembler::PopRegister(Register aRegister) {
  if (aRegister <= Register::RDI) {
    NewInstruction(0x58 + (int)aRegister - (int)Register::RAX);
  } else {
    NewInstruction(0x41, 0x58 + (int)aRegister - (int)Register::R8);
  }
}

void Assembler::MoveImmediateToRax(void* aValue) {
  uint8_t* ip = Current();
  ip[0] = 0x40 | (1 << 3);
  ip[1] = 0xB8;
  *reinterpret_cast<void**>(ip + 2) = aValue;
  Advance(10);
}

void Assembler::MoveRaxToRegister(Register aRegister) {
  if (aRegister <= Register::RDI) {
    NewInstruction(0x48, 0x89, 0xC0 + (int)aRegister - (int)Register::RAX);
  } else {
    NewInstruction(0x49, 0x89, 0xC0 + (int)aRegister - (int)Register::R8);
  }
}

void Assembler::MoveRegisterToRax(Register aRegister) {
  if (aRegister <= Register::RDI) {
    NewInstruction(0x48, 0x89, 0xC0 + ((int)aRegister - (int)Register::RAX) * 8);
  } else {
    NewInstruction(0x4C, 0x89, 0xC0 + ((int)aRegister - (int)Register::R8) * 8);
  }
}

void* BindFunctionArgument(void* aFunction, void* aArgument,
                           size_t aArgumentPosition, Assembler& aAssembler) {
  void* res = aAssembler.Current();

  // On x64 the argument will be in a register, so to add an extra argument for
  // the callee we just need to fill in the appropriate register for the
  // argument position with the bound argument value.
  aAssembler.MoveImmediateToRax(aArgument);

  switch (aArgumentPosition) {
    case 1:
      aAssembler.MoveRaxToRegister(Register::RSI);
      break;
    case 2:
      aAssembler.MoveRaxToRegister(Register::RDX);
      break;
    case 3:
      aAssembler.MoveRaxToRegister(Register::RCX);
      break;
    default:
      MOZ_CRASH();
  }

  // Jump to the function that was bound.
  aAssembler.Jump(aFunction);

  return res;
}

}  // namespace recordreplay
}  // namespace mozilla
