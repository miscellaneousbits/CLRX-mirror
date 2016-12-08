/*
 *  CLRadeonExtender - Unofficial OpenCL Radeon Extensions Library
 *  Copyright (C) 2014-2016 Mateusz Szpakowski
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <CLRX/Config.h>
#include <iostream>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <memory>
#include <CLRX/amdasm/Assembler.h>
#include "../TestUtils.h"

using namespace CLRX;

static void printHexData(std::ostream& os, cxuint indentLevel, size_t size,
             const cxbyte* data)
{
    if (data==nullptr)
    {
        for (cxuint j = 0; j < indentLevel; j++)
            os << "  ";
        os << "nullptr\n";
        return;
    }
    for (size_t i = 0; i < size; i++)
    {
        if ((i&31)==0)
            for (cxuint j = 0; j < indentLevel; j++)
                os << "  ";
        char buf[10];
        snprintf(buf, 10, "%02x", cxuint(data[i]));
        os << buf;
        if ((i&31)==31 || i+1 == size)
            os << '\n';
    }
}

static const char* rocmRegionTypeNames[3] =
{ "data", "fkernel", "kernel" };

static void printROCmOutput(std::ostream& os, const ROCmInput* output)
{
    os << "ROCmBinDump:" << std::endl;
    for (const ROCmSymbolInput& symbol: output->symbols)
    {
        os << "  ROCmSymbol: name=" << symbol.symbolName << ", " <<
                "offset=" << symbol.offset << "size=" << symbol.size << ", type=" <<
                rocmRegionTypeNames[cxuint(symbol.type)] << "\n";
        if (symbol.type == ROCmRegionType::DATA)
            continue;
        if (symbol.offset+sizeof(ROCmKernelConfig) > output->codeSize)
            continue;
        const ROCmKernelConfig& config = *reinterpret_cast<const ROCmKernelConfig*>(
                            output->code + symbol.offset);
        os << "    Config:\n"
            "      amdCodeVersion=" << ULEV(config.amdCodeVersionMajor) << "." <<
                ULEV(config.amdCodeVersionMajor) << "\n"
            "      amdMachine=" << ULEV(config.amdMachineKind) << ":" <<
                ULEV(config.amdMachineMajor) << ":" <<
                ULEV(config.amdMachineMinor) << ":" <<
                ULEV(config.amdMachineStepping) << "\n"
            "      kernelCodeEntryOffset=" << ULEV(config.kernelCodeEntryOffset) << "\n"
            "      kernelCodePrefetchOffset=" <<
                ULEV(config.kernelCodePrefetchOffset) << "\n"
            "      kernelCodePrefetchSize=" << ULEV(config.kernelCodePrefetchSize) << "\n"
            "      maxScrachBackingMemorySize=" <<
                ULEV(config.maxScrachBackingMemorySize) << "\n"
            "      computePgmRsrc1=0x" << std::hex << ULEV(config.computePgmRsrc1) << "\n"
            "      computePgmRsrc2=0x" << ULEV(config.computePgmRsrc2) << "\n"
            "      enableSpgrRegisterFlags=0x" <<
                ULEV(config.enableSpgrRegisterFlags) << "\n"
            "      enableFeatureFlags=0x" <<
                ULEV(config.enableFeatureFlags) << std::dec << "\n"
            "      workitemPrivateSegmentSize=" <<
                ULEV(config.workitemPrivateSegmentSize) << "\n"
            "      workgroupGroupSegmentSize=" <<
                ULEV(config.workgroupGroupSegmentSize) << "\n"
            "      gdsSegmentSize=" << ULEV(config.gdsSegmentSize) << "\n"
            "      kernargSegmentSize=" << ULEV(config.kernargSegmentSize) << "\n"
            "      workgroupFbarrierCount=" << ULEV(config.workgroupFbarrierCount) << "\n"
            "      wavefrontSgprCount=" << ULEV(config.wavefrontSgprCount) << "\n"
            "      workitemVgprCount=" << ULEV(config.workitemVgprCount) << "\n"
            "      reservedVgprFirst=" << ULEV(config.reservedVgprFirst) << "\n"
            "      reservedVgprCount=" << ULEV(config.reservedVgprCount) << "\n"
            "      reservedSgprFirst=" << ULEV(config.reservedSgprFirst) << "\n"
            "      reservedSgprCount=" << ULEV(config.reservedSgprCount) << "\n"
            "      debugWavefrontPrivateSegmentOffsetSgpr=" <<
                ULEV(config.debugWavefrontPrivateSegmentOffsetSgpr) << "\n"
            "      debugPrivateSegmentBufferSgpr=" <<
                ULEV(config.debugPrivateSegmentBufferSgpr) << "\n"
            "      kernargSegmentAlignment=" << 
                cxuint(config.kernargSegmentAlignment) << "\n"
            "      groupSegmentAlignment=" <<
                cxuint(config.groupSegmentAlignment) << "\n"
            "      privateSegmentAlignment=" <<
                cxuint(config.privateSegmentAlignment) << "\n"
            "      wavefrontSize=" << cxuint(config.wavefrontSize) << "\n"
            "      callConvention=0x" << std::hex << ULEV(config.callConvention) << "\n"
            "      runtimeLoaderKernelSymbol=0x" <<
                ULEV(config.runtimeLoaderKernelSymbol) << std::dec << "\n";
        os << "      ControlDirective:\n";
        printHexData(os, 3, 128, config.controlDirective);
    }
    os << "  Comment:\n";
    printHexData(os, 1, output->commentSize, (const cxbyte*)output->comment);
    os << "  Code:\n";
    printHexData(os, 1, output->codeSize, output->code);
    
    for (BinSection section: output->extraSections)
    {
        os << "  Section " << section.name << ", type=" << section.type <<
                        ", flags=" << section.flags << ":\n";
        printHexData(os, 1, section.size, section.data);
    }
    for (BinSymbol symbol: output->extraSymbols)
        os << "  Symbol: name=" << symbol.name << ", value=" << symbol.value <<
                ", size=" << symbol.size << ", section=" << symbol.sectionId << "\n";
    os.flush();
}


struct AsmTestCase
{
    const char* input;
    const char* dump;
    const char* errors;
    bool good;
};

static const AsmTestCase asmTestCases1Tbl[] =
{
    {
        R"ffDXD(        .rocm
        .gpu Fiji
.kernel kxx1
    .fkernel
    .config
        .codeversion 1,0
        .call_convention 0x34dac
        .debug_private_segment_buffer_sgpr 123834
        .debug_wavefront_private_segment_offset_sgpr 129
        .gds_segment_size 100
        .kernarg_segment_align 32
        .workgroup_group_segment_size 22
        .workgroup_fbarrier_count 3324
        .dx10clamp
        .exceptions 10
        .private_segment_align 128
        .privmode
        .reserved_sgpr_first 10
        .reserved_sgpr_count 5
        .runtime_loader_kernel_symbol 0x4dc98b3a
        .scratchbuffer 77222
        .reserved_sgpr_count 4
        .reserved_sgpr_first 9
        .reserved_vgpr_count 11
        .reserved_vgpr_first 7
        .private_elem_size 16
    .control_directive
        .int 1,2,3
        .fill 116,1,0
.kernel kxx2
    .config
        .codeversion 1,0
        .call_convention 0x112223
.kernel kxx1
    .config
        .scratchbuffer 111
.text
kxx1:
        .skip 256
        s_mov_b32 s7, 0
        s_endpgm
        
.align 256
kxx2:
        .skip 256
        s_endpgm
.section .comment
        .ascii "some comment for you"
.kernel kxx2
    .control_directive
        .fill 124,1,0xde
    .config
        .use_kernarg_segment_ptr
    .control_directive
        .int 0xaadd66cc
    .config
.kernel kxx1
.kernel kxx2
        .call_convention 0x1112234
        
)ffDXD",
        /* dump */
        R"ffDXD(ROCmBinDump:
  ROCmSymbol: name=kxx1, offset=0size=0, type=fkernel
    Config:
      amdCodeVersion=1.1
      amdMachine=1:8:0:0
      kernelCodeEntryOffset=256
      kernelCodePrefetchOffset=0
      kernelCodePrefetchSize=0
      maxScrachBackingMemorySize=0
      computePgmRsrc1=0x3c0000
      computePgmRsrc2=0xa0001ff
      enableSpgrRegisterFlags=0x0
      enableFeatureFlags=0x6
      workitemPrivateSegmentSize=111
      workgroupGroupSegmentSize=22
      gdsSegmentSize=100
      kernargSegmentSize=0
      workgroupFbarrierCount=3324
      wavefrontSgprCount=8
      workitemVgprCount=1
      reservedVgprFirst=7
      reservedVgprCount=11
      reservedSgprFirst=9
      reservedSgprCount=4
      debugWavefrontPrivateSegmentOffsetSgpr=129
      debugPrivateSegmentBufferSgpr=58298
      kernargSegmentAlignment=5
      groupSegmentAlignment=4
      privateSegmentAlignment=7
      wavefrontSize=6
      callConvention=0x34dac
      runtimeLoaderKernelSymbol=0x4dc98b3a
      ControlDirective:
      0100000002000000030000000000000000000000000000000000000000000000
      0000000000000000000000000000000000000000000000000000000000000000
      0000000000000000000000000000000000000000000000000000000000000000
      0000000000000000000000000000000000000000000000000000000000000000
  ROCmSymbol: name=kxx2, offset=512size=0, type=kernel
    Config:
      amdCodeVersion=1.1
      amdMachine=1:8:0:0
      kernelCodeEntryOffset=256
      kernelCodePrefetchOffset=0
      kernelCodePrefetchSize=0
      maxScrachBackingMemorySize=0
      computePgmRsrc1=0xc0000
      computePgmRsrc2=0x1fe
      enableSpgrRegisterFlags=0x8
      enableFeatureFlags=0x0
      workitemPrivateSegmentSize=0
      workgroupGroupSegmentSize=0
      gdsSegmentSize=0
      kernargSegmentSize=0
      workgroupFbarrierCount=0
      wavefrontSgprCount=2
      workitemVgprCount=1
      reservedVgprFirst=0
      reservedVgprCount=0
      reservedSgprFirst=0
      reservedSgprCount=0
      debugWavefrontPrivateSegmentOffsetSgpr=0
      debugPrivateSegmentBufferSgpr=0
      kernargSegmentAlignment=4
      groupSegmentAlignment=4
      privateSegmentAlignment=4
      wavefrontSize=6
      callConvention=0x1112234
      runtimeLoaderKernelSymbol=0x0
      ControlDirective:
      dededededededededededededededededededededededededededededededede
      dededededededededededededededededededededededededededededededede
      dededededededededededededededededededededededededededededededede
      dedededededededededededededededededededededededededededecc66ddaa
  Comment:
  736f6d6520636f6d6d656e7420666f7220796f75
  Code:
  0100000000000000010008000000000000010000000000000000000000000000
  0000000000000000000000000000000000003c00ff01000a000006006f000000
  16000000640000000000000000000000fc0c00000800010007000b0009000400
  8100bae305040706ac4d03000000000000000000000000003a8bc94d00000000
  0100000002000000030000000000000000000000000000000000000000000000
  0000000000000000000000000000000000000000000000000000000000000000
  0000000000000000000000000000000000000000000000000000000000000000
  0000000000000000000000000000000000000000000000000000000000000000
  800087be000081bf000080bf000080bf000080bf000080bf000080bf000080bf
  000080bf000080bf000080bf000080bf000080bf000080bf000080bf000080bf
  000080bf000080bf000080bf000080bf000080bf000080bf000080bf000080bf
  000080bf000080bf000080bf000080bf000080bf000080bf000080bf000080bf
  000080bf000080bf000080bf000080bf000080bf000080bf000080bf000080bf
  000080bf000080bf000080bf000080bf000080bf000080bf000080bf000080bf
  000080bf000080bf000080bf000080bf000080bf000080bf000080bf000080bf
  000080bf000080bf000080bf000080bf000080bf000080bf000080bf000080bf
  0100000000000000010008000000000000010000000000000000000000000000
  0000000000000000000000000000000000000c00fe0100000800000000000000
  0000000000000000000000000000000000000000020001000000000000000000
  0000000004040406342211010000000000000000000000000000000000000000
  dededededededededededededededededededededededededededededededede
  dededededededededededededededededededededededededededededededede
  dededededededededededededededededededededededededededededededede
  dedededededededededededededededededededededededededededecc66ddaa
  000081bf
)ffDXD",
        /* warning/errors */
        "",
        true
    }
};

static void testAssembler(cxuint testId, const AsmTestCase& testCase)
{
    std::istringstream input(testCase.input);
    std::ostringstream errorStream;
    std::ostringstream printStream;
    
    Assembler assembler("test.s", input, (ASM_ALL|ASM_TESTRUN)&~ASM_ALTMACRO,
            BinaryFormat::AMD, GPUDeviceType::CAPE_VERDE, errorStream, printStream);
    bool good = assembler.assemble();
    
    std::ostringstream dumpOss;
    if (good && assembler.getFormatHandler()!=nullptr)
        // get format handler and their output
        printROCmOutput(dumpOss, static_cast<const AsmROCmHandler*>(
                    assembler.getFormatHandler())->getOutput());
    /* compare results */
    char testName[30];
    snprintf(testName, 30, "Test #%u", testId);
    
    assertValue(testName, "good", int(testCase.good), int(good));
    assertString(testName, "dump", testCase.dump, dumpOss.str());
    assertString(testName, "errorMessages", testCase.errors, errorStream.str());
}

int main(int argc, const char** argv)
{
    int retVal = 0;
    for (size_t i = 0; i < sizeof(asmTestCases1Tbl)/sizeof(AsmTestCase); i++)
        try
        { testAssembler(i, asmTestCases1Tbl[i]); }
        catch(const std::exception& ex)
        {
            std::cerr << ex.what() << std::endl;
            retVal = 1;
        }
    return retVal;
}


