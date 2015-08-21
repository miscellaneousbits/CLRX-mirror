/*
 *  CLRadeonExtender - Unofficial OpenCL Radeon Extensions Library
 *  Copyright (C) 2014-2015 Mateusz Szpakowski
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
//#include <iostream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <CLRX/amdasm/Assembler.h>
#include "AsmInternals.h"
#include "GCNInternals.h"

using namespace CLRX;

static std::once_flag clrxGCNAssemblerOnceFlag;
static Array<GCNAsmInstruction> gcnInstrSortedTable;

static void initializeGCNAssembler()
{
    size_t tableSize = 0;
    while (gcnInstrsTable[tableSize].mnemonic!=nullptr)
        tableSize++;
    gcnInstrSortedTable.resize(tableSize);
    for (cxuint i = 0; i < tableSize; i++)
    {
        const GCNInstruction& insn = gcnInstrsTable[i];
        gcnInstrSortedTable[i] = {insn.mnemonic, insn.encoding, GCNENC_NONE, insn.mode,
                    insn.code, UINT16_MAX, insn.archMask};
    }
    
    std::sort(gcnInstrSortedTable.begin(), gcnInstrSortedTable.end(),
            [](const GCNAsmInstruction& instr1, const GCNAsmInstruction& instr2)
            {   // compare mnemonic and if mnemonic
                int r = ::strcmp(instr1.mnemonic, instr2.mnemonic);
                return (r < 0) || (r==0 && instr1.encoding1 < instr2.encoding1) ||
                            (r == 0 && instr1.encoding1 == instr2.encoding1 &&
                             instr1.archMask < instr2.archMask);
            });
    
    cxuint j = 0;
    /* join VOP3A instr with VOP2/VOPC/VOP1 instr together to faster encoding. */
    for (cxuint i = 0; i < tableSize; i++)
    {   
        GCNAsmInstruction insn = gcnInstrSortedTable[i];
        if ((insn.encoding1 == GCNENC_VOP3A || insn.encoding1 == GCNENC_VOP3B))
        {   // check duplicates
            cxuint k = j-1;
            while (::strcmp(gcnInstrSortedTable[k].mnemonic, insn.mnemonic)==0 &&
                    (gcnInstrSortedTable[k].archMask & insn.archMask)!=insn.archMask) k--;
            
            if (::strcmp(gcnInstrSortedTable[k].mnemonic, insn.mnemonic)==0 &&
                (gcnInstrSortedTable[k].archMask & insn.archMask)==insn.archMask)
            {   // we found duplicate, we apply
                if (gcnInstrSortedTable[k].code2==UINT16_MAX)
                {   // if second slot for opcode is not filled
                    gcnInstrSortedTable[k].code2 = insn.code1;
                    gcnInstrSortedTable[k].encoding2 = insn.encoding1;
                    gcnInstrSortedTable[k].archMask &= insn.archMask;
                }
                else
                {   // if filled we create new entry
                    gcnInstrSortedTable[j] = gcnInstrSortedTable[k];
                    gcnInstrSortedTable[j].archMask &= insn.archMask;
                    gcnInstrSortedTable[j].encoding2 = insn.encoding1;
                    gcnInstrSortedTable[j++].code2 = insn.code1;
                }
            }
            else // not found
                gcnInstrSortedTable[j++] = insn;
        }
        else // normal instruction
            gcnInstrSortedTable[j++] = insn;
    }
    gcnInstrSortedTable.resize(j); // final size
    /* for (const GCNAsmInstruction& instr: gcnInstrSortedTable)
        std::cout << "{ " << instr.mnemonic << ", " << cxuint(instr.encoding1) << ", " <<
                cxuint(instr.encoding2) <<
                std::hex << ", 0x" << instr.mode << ", 0x" << instr.code1 << ", 0x" <<
                instr.code2 << std::dec << ", " << instr.archMask << " }" << std::endl;*/
}

GCNAssembler::GCNAssembler(Assembler& assembler): ISAAssembler(assembler),
        sgprsNum(0), vgprsNum(0), curArchMask(
                    1U<<cxuint(getGPUArchitectureFromDeviceType(assembler.getDeviceType())))
{
    std::call_once(clrxGCNAssemblerOnceFlag, initializeGCNAssembler);
}

GCNAssembler::~GCNAssembler()
{ }

static cxbyte cstrtobyte(const char*& str, const char* end)
{
    uint16_t value = 0;
    if (str==end || !isDigit(*str))
        throw ParseException("Missing number");
    while (str!=end && isDigit(*str))
    {
        value = value*10 + *str-'0';
        if (value >= 256)
            throw ParseException("Number is too big");
    }
    return value;
}

namespace CLRX
{

RegPair GCNAsmUtils::parseVRegRange(Assembler& asmr, const char*& linePtr, bool required)
{
    const char* end = asmr.line+asmr.lineSize;
    skipSpacesToEnd(linePtr, end);
    const char* vgprRangePlace = linePtr;
    if (linePtr == end)
    {
        if (required)
            asmr.printError(vgprRangePlace, "VRegister range is required");
        return std::make_pair(0, 0);
    }
    if (toLower(*linePtr) != 'v') // if
    {
        if (required)
            asmr.printError(vgprRangePlace, "VRegister range is required");
        return std::make_pair(0, 0);
    }
    if (++linePtr == end)
    {
        if (required)
            asmr.printError(vgprRangePlace, "VRegister range is required");
        return std::make_pair(0, 0);
    }
    
    try /* for handling parse exception */
    {
    if (isDigit(*vgprRangePlace))
    {   // if single register
        cxuint value = cstrtobyte(linePtr, end);
        if (value >= 256)
        {
            asmr.printError(vgprRangePlace, "VRegister number of out range (0-255)");
            std::make_pair(0, 0);
        }
        return std::make_pair(256+value, 256+value+1);
    }
    else if (*linePtr == '[')
    {   // many registers
        ++linePtr;
        cxuint value1, value2;
        skipSpacesToEnd(linePtr, end);
        value1 = cstrtobyte(linePtr, end);
        skipSpacesToEnd(linePtr, end);
        if (linePtr == end || *linePtr != ':')
        {   // error
            asmr.printError(vgprRangePlace, "Unterminated VRegister range");
            return std::make_pair(0, 0);
        }
        ++linePtr;
        skipSpacesToEnd(linePtr, end);
        value2 = cstrtobyte(linePtr, end);
        if (value2 <= value1 || value2 >= 256 || value1 >= 256)
        {   // error (illegal register range)
            asmr.printError(vgprRangePlace, "Illegal VRegister range");
            return std::make_pair(0, 0);
        }
        skipSpacesToEnd(linePtr, end);
        if (linePtr == end || *linePtr != ']')
        {   // error
            asmr.printError(vgprRangePlace, "Unterminated VRegister range");
            return std::make_pair(0, 0);
        }
        return std::make_pair(256+value1, 256+value2+1);
    }
    } catch(const ParseException& ex)
    { asmr.printError(linePtr, ex.what()); }
    
    return std::make_pair(0, 0);
}

RegPair GCNAsmUtils::parseSRegRange(Assembler& asmr, const char*& linePtr,
                    uint16_t arch, bool required)
{
    const char* end = asmr.line+asmr.lineSize;
    skipSpacesToEnd(linePtr, end);
    const char* sgprRangePlace = linePtr;
    if (linePtr == end)
    {
        if (required)
            asmr.printError(sgprRangePlace, "SRegister range is required");
        return std::make_pair(0, 0);
    }
    
    try
    {
    if (toLower(*linePtr) != 's') // if
    {
        char regName[20];
        if (!getNameArg(asmr, 20, regName, linePtr, "register name", required, false))
            return std::make_pair(0, 0);
        toLowerString(regName);
        
        size_t loHiRegSuffix = 0;
        cxuint loHiReg = 0;
        if (regName[0] == 'v' && regName[1] == 'c' && regName[2] == 'c')
        {   /// vcc
            loHiRegSuffix = 3;
            loHiReg = 106;
        }
        else if (regName[0] == 'e' && regName[1] == 'x' && regName[2] == 'e' &&
            regName[3] == 'c')
        {   /* exec* */
            loHiRegSuffix = 4;
            loHiReg = 126;
        }
        else if (regName[0]=='t')
        {   /* tma,tba, ttmpX */
            if (regName[1] == 'b' && regName[2] == 'a')
            {
                loHiRegSuffix = 3;
                loHiReg = 108;
            }
            else if (regName[1] == 'm' && regName[2] == 'a')
            {
                loHiRegSuffix = 3;
                loHiReg = 110;
            }
            else if (regName[1] == 't' && regName[2] == 'm' && regName[3] == 'p')
            {
                cxbyte value = cstrtobyte(linePtr, end);
                if (value > 11)
                {
                    asmr.printError(sgprRangePlace, "TTMP register number out of range");
                    return std::make_pair(0, 0);
                }
            }
        }
        else if (regName[0] == 'm' || regName[1] == '0' || regName[2] == 0)
            return std::make_pair(124, 125);
        else if (arch&ARCH_GCN_1_1_2)
        {
            if (::memcmp(regName, "flat_scratch", 12)==0)
            {   // flat
                loHiRegSuffix = 12;
                loHiReg = (arch&ARCH_RX3X0)?102:104;
            }
            else if ((arch&ARCH_RX3X0)!=0 && ::memcmp(regName, "xnack_mask", 10)==0)
            {   // xnack
                loHiRegSuffix = 10;
                loHiReg = 104;
            }
        }
        
        if (loHiRegSuffix != 0) // handle 64-bit registers
        {
            if (regName[loHiRegSuffix] == '_')
            {
                if (regName[loHiRegSuffix+1] == 'l' && regName[loHiRegSuffix+2] == 'o' &&
                    regName[loHiRegSuffix+3] == 0)
                    return std::make_pair(loHiReg, loHiReg+1);
                if (regName[loHiRegSuffix+1] == 'h' && regName[loHiRegSuffix+2] == 'i' &&
                    regName[loHiRegSuffix+3] == 0)
                    return std::make_pair(loHiReg+1, loHiReg+2);
            }
            else if (regName[loHiRegSuffix] == 0)
                return std::make_pair(loHiReg, loHiReg+2);
        }
        else
        {   // otherwise
            if (required)
                asmr.printError(sgprRangePlace, "SRegister range is required");
            return std::make_pair(0, 0);
        }
    }
    if (++linePtr == end)
    {
        if (required)
            asmr.printError(sgprRangePlace, "SRegister range is required");
        return std::make_pair(0, 0);
    }
    
    if (isDigit(*linePtr))
    {   // if single register
        cxuint value = cstrtobyte(linePtr, end);
        return std::make_pair(value, value+1);
    }
    else if (*linePtr == '[')
    {   // many registers
        ++linePtr;
        cxuint value1, value2;
        skipSpacesToEnd(linePtr, end);
        value1 = cstrtobyte(linePtr, end);
        skipSpacesToEnd(linePtr, end);
        if (linePtr == end || *linePtr != ':')
        {   // error
            asmr.printError(sgprRangePlace, "Unterminated SRegister range");
            return std::make_pair(0, 0);
        }
        ++linePtr;
        skipSpacesToEnd(linePtr, end);
        value2 = cstrtobyte(linePtr, end);
        
        const cxuint maxSGPRsNum = (arch&ARCH_RX3X0) ? 102 : 104;
        if (value2 <= value1 || value1 >= maxSGPRsNum || value2 >= maxSGPRsNum)
        {   // error (illegal register range)
            asmr.printError(sgprRangePlace, "Illegal SRegister range");
            return std::make_pair(0, 0);
        }
        skipSpacesToEnd(linePtr, end);
        if (linePtr == end || *linePtr != ']')
        {   // error
            asmr.printError(sgprRangePlace, "Unterminated SRegister range");
            return std::make_pair(0, 0);
        }
        /// check alignment
        if ((value2-value1==1 && (value1&1)!=0) ||
            (value2-value1>1 && (value1&3)!=0))
        {
            asmr.printError(sgprRangePlace, "Unaligned SRegister range");
            return std::make_pair(0, 0);
        }
        return std::make_pair(value1, uint16_t(value2)+1);
    }
    } catch(const ParseException& ex)
    { asmr.printError(linePtr, ex.what()); }
    
    return std::make_pair(0, 0);
}

/* check whether string is exclusively floating point value
 * (only floating point, and neither integer and nor symbol) */
static bool isOnlyFloat(const char* str, const char* end)
{
    if (str == end)
        return false;
    if (*str=='-' || *str=='+')
        str++; // skip '-' or '+'
    if (str+2 >= end && *str=='0' && (str[1]=='X' || str[1]=='x'))
    {   // hexadecimal
        str += 2;
        const char* beforeComma = str;
        while (str!=end && isXDigit(*str)) str++;
        const char* point = str;
        if (str == end || *str!='.')
        {
            if (beforeComma-point!=0 && str!=end && (*str=='p' || *str=='P'))
            {
                str++;
                if (str!=end && (*str=='-' || *str=='+'))
                    str++;
                const char* expPlace = str;
                while (str!=end && isDigit(*str)) str++;
                if (str-expPlace!=0)
                    return true; // if 'XXXp[+|-]XXX'
            }
            return false; // no '.'
        }
        str++;
        while (str!=end && isXDigit(*str)) str++;
        const char* afterComma = str;
        
        if (point-beforeComma!=0 || afterComma-(point+1)!=0)
            return true;
    }
    else
    {   // decimal
        const char* beforeComma = str;
        while (str!=end && isDigit(*str)) str++;
        const char* point = str;
        if (str == end || *str!='.')
        {
            if (beforeComma-point!=0 && str!=end && (*str=='e' || *str=='E'))
            {
                str++;
                if (str!=end && (*str=='-' || *str=='+'))
                    str++;
                const char* expPlace = str;
                while (str!=end && isDigit(*str)) str++;
                if (str-expPlace!=0)
                    return true; // if 'XXXe[+|-]XXX'
            }
            return false; // no '.'
        }
        str++;
        while (str!=end && isDigit(*str)) str++;
        const char* afterComma = str;
        
        if (point-beforeComma!=0 || afterComma-(point+1)!=0)
            return true;
    }
    return false;
}

GCNOperand GCNAsmUtils::parseOperand(Assembler& asmr, const char*& linePtr,
             std::unique_ptr<AsmExpression>& outTargetExpr, uint16_t arch,
             Flags instrOpMask)
{
    outTargetExpr.reset();
    
    if (instrOpMask == INSTROP_SREGS)
        return { parseSRegRange(asmr, linePtr, arch) };
    else if (instrOpMask == INSTROP_VREGS)
        return { parseVRegRange(asmr, linePtr) };
    // otherwise
    RegPair pair;
    if (instrOpMask & INSTROP_SREGS)
    {
        pair = parseSRegRange(asmr, linePtr, arch, false);
        if (pair.first!=0 || pair.second!=0)
            return { pair };
    }
    if (instrOpMask & INSTROP_SSOURCE)
    {   // check rest of the positions in SSRC
    }
    if (instrOpMask & INSTROP_VREGS)
    {
        pair = parseVRegRange(asmr, linePtr, false);
        if (pair.first!=0 || pair.second!=0)
            return { pair };
    }
    
    const char* end = asmr.line+asmr.lineSize;
    if ((instrOpMask & INSTROP_SSOURCE)!=0)
    {   char regName[20];
        const char* regNamePlace = linePtr;
        if (getNameArg(asmr, 20, regName, linePtr, "register name", false, false))
        {
            toLowerString(regName);
            if (::memcmp(regName, "vccz", 5) == 0)
                return { std::make_pair(251, 252) };
            else if (::memcmp(regName, "execz", 6) == 0)
                return { std::make_pair(252, 253) };
            else if (::memcmp(regName, "scc", 4) == 0)
                return { std::make_pair(253, 254) };
            /* check expression, back to before regName */
            linePtr = regNamePlace;
        }
        // treat argument as expression
        if (linePtr!=end && *linePtr=='@')
            linePtr++;
        skipSpacesToEnd(linePtr, end);
        
        uint64_t value;
        if (isOnlyFloat(linePtr, end))
        {   // if only floating point value
            /* if floating point literal can be processed */
            try
            {
                if ((instrOpMask & INSTROP_TYPE_MASK) == INSTROP_F16)
                {
                    value = cstrtohCStyle(linePtr, end, linePtr);
                    switch (value)
                    {
                        case 0x0:
                            return { std::make_pair(128, 0) };
                        case 0x3800: // 0.5
                            return { std::make_pair(240, 0) };
                        case 0xb800: // -0.5
                            return { std::make_pair(241, 0) };
                        case 0x3c00: // 1.0
                            return { std::make_pair(242, 0) };
                        case 0xbc00: // -1.0
                            return { std::make_pair(243, 0) };
                        case 0x4000: // 2.0
                            return { std::make_pair(244, 0) };
                        case 0xc000: // -2.0
                            return { std::make_pair(245, 0) };
                        case 0x4400: // 4.0
                            return { std::make_pair(246, 0) };
                        case 0xc400: // -4.0
                            return { std::make_pair(247, 0) };
                        case 0x3118: // 1/(2*PI)
                            if (arch&ARCH_RX3X0)
                                return { std::make_pair(248, 0) };
                    }
                }
                else /* otherwise, FLOAT */
                {
                    union FloatUnion { uint32_t i; float f; };
                    FloatUnion v;
                    v.f = cstrtovCStyle<float>(linePtr, end, linePtr);
                    value = v.i;
                    
                    switch (value)
                    {
                        case 0x0:
                            return { std::make_pair(128, 0) };
                        case 0x3f000000: // 0.5
                            return { std::make_pair(240, 0) };
                        case 0xbf000000: // -0.5
                            return { std::make_pair(241, 0) };
                        case 0x3f800000: // 1.0
                            return { std::make_pair(242, 0) };
                        case 0xbf800000: // -1.0
                            return { std::make_pair(243, 0) };
                        case 0x40000000: // 2.0
                            return { std::make_pair(244, 0) };
                        case 0xc0000000: // -2.0
                            return { std::make_pair(245, 0) };
                        case 0x40800000: // 4.0
                            return { std::make_pair(246, 0) };
                        case 0xc0800000: // -4.0
                            return { std::make_pair(247, 0) };
                        case 0x3e22f983: // 1/(2*PI)
                            if (arch&ARCH_RX3X0)
                                return { std::make_pair(248, 0) };
                    }
                }
                
            }
            catch(const ParseException& ex)
            {
                asmr.printError(regNamePlace, ex.what());
                return { std::make_pair(0, 0) };
            }
        }
        else
        {   // if expression
            const char* exprPlace = linePtr;
            std::unique_ptr<AsmExpression> expr(AsmExpression::parse( asmr, linePtr));
            if (expr==nullptr) // error
                return { std::make_pair(0, 0) };
            if (expr->isEmpty())
            {
                asmr.printError(exprPlace, "Expected expression");
                return { std::make_pair(0, 0) };
            }
            if (expr->getSymOccursNum()==0)
            {   // resolved now
                cxuint sectionId; // for getting
                if (!expr->evaluate(asmr, value, sectionId)) // failed evaluation!
                    return { std::make_pair(0, 0) };
                else if (sectionId != ASMSECT_ABS)
                {   // if not absolute value
                    asmr.printError(exprPlace, "Expression must be absolute!");
                    return { std::make_pair(0, 0) };
                }
            }
            else
            {   // return output expression with symbols to resolve
                if ((instrOpMask & INSTROP_ONLYINLINECONSTS)!=0)
                {   // error
                    asmr.printError(regNamePlace,
                            "Literal constant is illegal in this place");
                    return { std::make_pair(0, 0) };
                }
                outTargetExpr = std::move(expr);
                return { std::make_pair(255, 0) };
            }
            
            if (!getAbsoluteValueArg(asmr, value, linePtr, true))
                return { std::make_pair(0, 0) };
            if (value <= 64)
                return { std::make_pair(128+value, 0) };
            else if (int64_t(value) >= -16)
                return { std::make_pair(192-value, 0) };
        }
        
        if ((instrOpMask & INSTROP_ONLYINLINECONSTS)!=0)
        {   // error
            asmr.printError(regNamePlace, "Literal constant is illegal in this place");
            return { std::make_pair(0, 0) };
        }
        
        // not in range
        asmr.printWarningForRange(32, value, asmr.getSourcePos(regNamePlace));
        return { std::make_pair(255, 0), uint32_t(value) };
    }
    
    // check otherwise
    return { std::make_pair(0, 0) };
}

void GCNAsmUtils::parseSOP2Encoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseSOP1Encoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseSOPKEncoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseSOPCEncoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseSOPPEncoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseSMRDEncoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseVOP2Encoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseVOP1Encoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseVOPCEncoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseVOP3Encoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseVINTRPEncoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseDSEncoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseMXBUFEncoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseMIMGEncoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseEXPEncoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

void GCNAsmUtils::parseFLATEncoding(Assembler& asmr, const GCNAsmInstruction& insn,
                  const char* linePtr, std::vector<cxbyte>& output)
{
}

};

void GCNAssembler::assemble(const CString& mnemonic, const char* mnemPlace,
            const char* linePtr, const char* lineEnd, std::vector<cxbyte>& output)
{
    auto it = binaryFind(gcnInstrSortedTable.begin(), gcnInstrSortedTable.end(),
               GCNAsmInstruction{mnemonic.c_str()},
               [](const GCNAsmInstruction& instr1, const GCNAsmInstruction& instr2)
               { return ::strcmp(instr1.mnemonic, instr2.mnemonic)<0; });
    
    // find matched entry
    if (it != gcnInstrSortedTable.end() && (it->archMask & curArchMask)==0)
        // if not match current arch mask
        for (++it ;it != gcnInstrSortedTable.end() &&
               ::strcmp(it->mnemonic, mnemonic.c_str())==0 &&
               (it->archMask & curArchMask)==0; ++it);

    if (it == gcnInstrSortedTable.end() || ::strcmp(it->mnemonic, mnemonic.c_str())!=0)
    {   // unrecognized mnemonic
        printError(mnemPlace, "Unrecognized instruction");
        return;
    }
    
    /* decode instruction line */
    switch(it->encoding1)
    {
        case GCNENC_SOPC:
            GCNAsmUtils::parseSOPCEncoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_SOPP:
            GCNAsmUtils::parseSOPPEncoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_SOP1:
            GCNAsmUtils::parseSOP1Encoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_SOP2:
            GCNAsmUtils::parseSOP2Encoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_SOPK:
            GCNAsmUtils::parseSOPKEncoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_SMRD:
            GCNAsmUtils::parseSMRDEncoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_VOPC:
            GCNAsmUtils::parseVOPCEncoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_VOP1:
            GCNAsmUtils::parseVOP1Encoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_VOP2:
            GCNAsmUtils::parseVOP2Encoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_VOP3A:
        case GCNENC_VOP3B:
            GCNAsmUtils::parseVOP3Encoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_VINTRP:
            GCNAsmUtils::parseVINTRPEncoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_DS:
            GCNAsmUtils::parseDSEncoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_MUBUF:
        case GCNENC_MTBUF:
            GCNAsmUtils::parseMXBUFEncoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_MIMG:
            GCNAsmUtils::parseMIMGEncoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_EXP:
            GCNAsmUtils::parseEXPEncoding(assembler, *it, linePtr, output);
            break;
        case GCNENC_FLAT:
            GCNAsmUtils::parseFLATEncoding(assembler, *it, linePtr, output);
            break;
        default:
            break;
    }
}

bool GCNAssembler::resolveCode(cxbyte* location, cxbyte targetType, uint64_t value)
{
    return false;
}

bool GCNAssembler::checkMnemonic(const CString& mnemonic) const
{
    return std::binary_search(gcnInstrSortedTable.begin(), gcnInstrSortedTable.end(),
               GCNAsmInstruction{mnemonic.c_str()},
               [](const GCNAsmInstruction& instr1, const GCNAsmInstruction& instr2)
               { return ::strcmp(instr1.mnemonic, instr2.mnemonic)<0; });
}

const cxuint* GCNAssembler::getAllocatedRegisters(size_t& regTypesNum) const
{
    regTypesNum = 2;
    return regTable;
}
