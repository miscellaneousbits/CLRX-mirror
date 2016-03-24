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
/*! \file Disassembler.h
 * \brief an disassembler for Radeon GPU's
 */

#ifndef __CLRX_DISASSEMBLER_H__
#define __CLRX_DISASSEMBLER_H__

#include <CLRX/Config.h>
#include <string>
#include <istream>
#include <ostream>
#include <vector>
#include <utility>
#include <memory>
#include <CLRX/amdbin/AmdBinaries.h>
#include <CLRX/amdbin/AmdCL2Binaries.h>
#include <CLRX/amdbin/GalliumBinaries.h>
#include <CLRX/amdbin/AmdBinGen.h>
#include <CLRX/amdasm/Commons.h>
#include <CLRX/utils/Utilities.h>
#include <CLRX/utils/InputOutput.h>

/// main namespace
namespace CLRX
{

class Disassembler;

enum: Flags
{
    DISASM_DUMPCODE = 1,    ///< dump code
    DISASM_METADATA = 2,    ///< dump metadatas
    DISASM_DUMPDATA = 4,    ///< dump datas
    DISASM_CALNOTES = 8,    ///< dump ATI CAL notes
    DISASM_FLOATLITS = 16,  ///< print in comments float literals
    DISASM_HEXCODE = 32,    ///< print on left side hexadecimal code
    DISASM_SETUP = 64,
    DISASM_ALL = FLAGS_ALL       ///< all disassembler flags
};

/// main class for
class ISADisassembler: public NonCopyableAndNonMovable
{
protected:
    struct Relocation
    {
        size_t symbol;   ///< symbol index
        RelocType type; ///< relocation type
        int64_t addend; ///< relocation addend
    };
    
    typedef std::vector<size_t>::const_iterator LabelIter;  ///< label iterator
    
    /// relocation iterator
    typedef std::vector<std::pair<size_t, Relocation> >::const_iterator RelocIter;
    
    /// named label iterator
    typedef std::vector<std::pair<size_t, CString> >::const_iterator NamedLabelIter;
    
    Disassembler& disassembler; ///< disassembler instance
    size_t inputSize;   ///< size of input
    const cxbyte* input;    ///< input code
    std::vector<size_t> labels; ///< list of local labels
    std::vector<std::pair<size_t, CString> > namedLabels;   ///< named labels
    std::vector<CString> relSymbols;    ///< symbols used by relocations
    std::vector<std::pair<size_t, Relocation> > relocations;    ///< relocations
    FastOutputBuffer output;    ///< output buffer
    
    /// constructor
    explicit ISADisassembler(Disassembler& disassembler, cxuint outBufSize = 500);
    
    /// write all labels before specified position
    void writeLabelsToPosition(size_t pos, LabelIter& labelIter,
               NamedLabelIter& namedLabelIter);
    /// write all labels to end
    void writeLabelsToEnd(size_t start, LabelIter labelIter, NamedLabelIter namedLabelIter);
    /// write location in the code
    void writeLocation(size_t pos);
    /// write relocation to current place in instruction
    bool writeRelocation(size_t pos, RelocIter& relocIter);
public:
    virtual ~ISADisassembler();
    
    /// set input code
    void setInput(size_t inputSize, const cxbyte* input)
    {
        this->inputSize = inputSize;
        this->input = input;
    }

    /// makes some things before disassemblying
    virtual void beforeDisassemble() = 0;
    /// disassembles input code
    virtual void disassemble() = 0;

    /// add named label to list (must be called before disassembly)
    void addNamedLabel(size_t pos, const CString& name)
    { namedLabels.push_back(std::make_pair(pos, name)); }
    /// add named label to list (must be called before disassembly)
    void addNamedLabel(size_t pos, CString&& name)
    { namedLabels.push_back(std::make_pair(pos, name)); }
    
    /// add symbol to relocations
    size_t addRelSymbol(const CString& symName)
    {
        size_t index = relSymbols.size();
        relSymbols.push_back(symName);
        return index;
    }
    /// add relocation
    void addRelocation(size_t offset, RelocType type, size_t symIndex, int64_t addend)
    { relocations.push_back(std::make_pair(offset, Relocation{symIndex, type, addend})); }
    
    void clearRelocations()
    {
        relSymbols.clear();
        relocations.clear();
    }
};

struct GCNDisasmUtils;

/// GCN architectur dissassembler
class GCNDisassembler: public ISADisassembler
{
private:
    bool instrOutOfCode;
    
    friend struct GCNDisasmUtils; // INTERNAL LOGIC
public:
    /// constructor
    GCNDisassembler(Disassembler& disassembler);
    /// destructor
    ~GCNDisassembler();
    
    /// routine called before main disassemblying
    void beforeDisassemble();
    /// disassemble code
    void disassemble();
};

/// single kernel input for disassembler
/** all pointer members holds only pointers that should be freed by your routines.
 * No management of data */
struct AmdDisasmKernelInput
{
    CString kernelName; ///< kernel name
    size_t metadataSize;    ///< metadata size
    const char* metadata;   ///< kernel's metadata
    size_t headerSize;  ///< kernel header size
    const cxbyte* header;   ///< kernel header size
    std::vector<CALNoteInput> calNotes;   ///< ATI CAL notes
    size_t dataSize;    ///< data (from inner binary) size
    const cxbyte* data; ///< data from inner binary
    size_t codeSize;    ///< size of code of kernel
    const cxbyte* code; ///< code of kernel
};

/// whole disassembler input (for AMD Catalyst driver GPU binaries)
/** all pointer members holds only pointers that should be freed by your routines.
 * No management of data */
struct AmdDisasmInput
{
    GPUDeviceType deviceType;   ///< GPU device type
    bool is64BitMode;       ///< true if 64-bit mode of addressing
    CString driverInfo; ///< driver info (for AMD Catalyst drivers)
    CString compileOptions; ///< compile options which used by in clBuildProgram
    size_t globalDataSize;  ///< global (constants for kernels) data size
    const cxbyte* globalData;   ///< global (constants for kernels) data
    std::vector<AmdDisasmKernelInput> kernels;    ///< kernel inputs
};

struct AmdCL2RelaEntry
{
    size_t offset;
    RelocType type;
    cxuint symbol;
    int64_t addend;
};

/// single kernel input for disassembler
/** all pointer members holds only pointers that should be freed by your routines.
 * No management of data */
struct AmdCL2DisasmKernelInput
{
    CString kernelName; ///< kernel name
    size_t metadataSize;    ///< metadata size
    const cxbyte* metadata;   ///< kernel's metadata
    size_t isaMetadataSize;    ///< metadata size
    const cxbyte* isaMetadata;   ///< kernel's metadata
    size_t setupSize;    ///< data (from inner binary) size
    const cxbyte* setup; ///< data from inner binary
    size_t stubSize;    ///< data (from inner binary) size
    const cxbyte* stub; ///< data from inner binary
    std::vector<AmdCL2RelaEntry> textRelocs;    ///< text relocations
    size_t codeSize;    ///< size of code of kernel
    const cxbyte* code; ///< code of kernel
};

/// whole disassembler input (for AMD Catalyst driver GPU binaries)
/** all pointer members holds only pointers that should be freed by your routines.
 * No management of data */
struct AmdCL2DisasmInput
{
    GPUDeviceType deviceType;   ///< GPU device type
    bool newDriver;             ///< if generated by/for new driver (>=Radeon Crimson)
    CString compileOptions; ///< compile options which used by in clBuildProgram
    CString aclVersionString; ///< acl version string
    size_t globalDataSize;  ///< global (constants for kernels) data size
    const cxbyte* globalData;   ///< global (constants for kernels) data
    size_t atomicDataSize;  ///< global atomic data size
    const cxbyte* atomicData;   ///< global atomic data data
    size_t samplerInitSize;     ///< sampler init data size
    const cxbyte* samplerInit;  ///< sampler init data
    /// sampler relocations
    std::vector<std::pair<size_t, size_t> > samplerRelocs;
    std::vector<AmdCL2DisasmKernelInput> kernels;    ///< kernel inputs
};


/// whole disassembler input (for Gallium driver GPU binaries)
struct GalliumDisasmInput
{
    GPUDeviceType deviceType;   ///< GPU device type
    size_t globalDataSize;  ///< global (constants for kernels) data size
    const cxbyte* globalData;   ///< global (constants for kernels) data
    std::vector<GalliumDisasmKernelInput> kernels;    ///< list of input kernels
    size_t codeSize;    ///< code size
    const cxbyte* code; ///< code
};

/// disassembler input for raw code
struct RawCodeInput
{
    GPUDeviceType deviceType;   ///< GPU device type
    size_t codeSize;            ///< code size
    const cxbyte* code;         ///< code
};

/// disassembler class
class Disassembler: public NonCopyableAndNonMovable
{
private:
    friend class ISADisassembler;
    std::unique_ptr<ISADisassembler> isaDisassembler;
    bool fromBinary;
    BinaryFormat binaryFormat;
    union {
        const AmdDisasmInput* amdInput;
        const AmdCL2DisasmInput* amdCL2Input;
        const GalliumDisasmInput* galliumInput;
        const RawCodeInput* rawInput;
    };
    std::ostream& output;
    Flags flags;
    size_t sectionCount;
    
    void disassembleAmd(); // Catalyst format
    void disassembleAmdCL2(); // Catalyst OpenCL 2.0 format
    void disassembleGallium(); // Gallium format
    void disassembleRawCode(); // raw code format
public:
    /// constructor for 32-bit GPU binary
    /**
     * \param binary main GPU binary
     * \param output output stream
     * \param flags flags for disassembler
     */
    Disassembler(const AmdMainGPUBinary32& binary, std::ostream& output,
                 Flags flags = 0);
    /// constructor for 64-bit GPU binary
    /**
     * \param binary main GPU binary
     * \param output output stream
     * \param flags flags for disassembler
     */
    Disassembler(const AmdMainGPUBinary64& binary, std::ostream& output,
                 Flags flags = 0);
    /// constructor for AMD OpenCL 2.0 GPU binary
    /**
     * \param binary main GPU binary
     * \param output output stream
     * \param flags flags for disassembler
     */
    Disassembler(const AmdCL2MainGPUBinary& binary, std::ostream& output,
                 Flags flags = 0);
    /// constructor for AMD disassembler input
    /**
     * \param disasmInput disassembler input object
     * \param output output stream
     * \param flags flags for disassembler
     */
    Disassembler(const AmdDisasmInput* disasmInput, std::ostream& output,
                 Flags flags = 0);
    /// constructor for AMD OpenCL 2.0 disassembler input
    /**
     * \param disasmInput disassembler input object
     * \param output output stream
     * \param flags flags for disassembler
     */
    Disassembler(const AmdCL2DisasmInput* disasmInput, std::ostream& output,
                 Flags flags = 0);
    
    /// constructor for bit GPU binary from Gallium
    /**
     * \param deviceType GPU device type
     * \param binary main GPU binary
     * \param output output stream
     * \param flags flags for disassembler
     */
    Disassembler(GPUDeviceType deviceType, const GalliumBinary& binary,
                 std::ostream& output, Flags flags = 0);
    
    /// constructor for Gallium disassembler input
    /**
     * \param disasmInput disassembler input object
     * \param output output stream
     * \param flags flags for disassembler
     */
    Disassembler(const GalliumDisasmInput* disasmInput, std::ostream& output,
                 Flags flags = 0);
    
    /// constructor for raw code
    Disassembler(GPUDeviceType deviceType, size_t rawCodeSize, const cxbyte* rawCode,
                 std::ostream& output, Flags flags = 0);
    
    ~Disassembler();
    
    /// disassembles input
    void disassemble();
    
    /// get disassemblers flags
    Flags getFlags() const
    { return flags; }
    /// get disassemblers flags
    void setFlags(Flags flags)
    { this->flags = flags; }
    
    /// get deviceType
    GPUDeviceType getDeviceType() const;
    
    /// get disassembler input
    const AmdDisasmInput* getAmdInput() const
    { return amdInput; }
    
    /// get disassembler input
    const AmdCL2DisasmInput* getAmdCL2Input() const
    { return amdCL2Input; }
    
    /// get disassembler input
    const GalliumDisasmInput* getGalliumInput() const
    { return galliumInput; }
    
    /// get output stream
    const std::ostream& getOutput() const
    { return output; }
    /// get output stream
    std::ostream& getOutput()
    { return output; }
};

};

#endif
