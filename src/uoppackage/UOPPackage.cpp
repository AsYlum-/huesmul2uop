#include "uoppackage.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <exception>

#include "uophash.h"
#include "uopblock.h"
#include "uopfile.h"

#define ADDERROR(str) UOPError::append((str), errorQueue)


namespace uopp
{


UOPPackage::UOPPackage(unsigned int version, unsigned int maxFilesPerBlock) :
    m_version(version),
    m_misc(0xFD23EC43), m_startAddress(0),
    m_blockSize(maxFilesPerBlock), m_fileCount(0),
    m_curBlockIdx(0)
{
    if ((m_version < kMinSupportedVersion) || (m_version > kMaxSupportedVersion))
        throw std::logic_error("Trying to construct UOPPackage with unsupported version=" + std::to_string(m_version));
}

UOPPackage::~UOPPackage()
{
    for (UOPBlock *block : m_blocks)
        delete block;
}


UOPFile* UOPPackage::getFileByIndex(unsigned int block, unsigned int index) const
{
    if ((m_blocks.size() < block) || (m_blocks[block]->getFilesCount() < index) )
        return nullptr;
    return m_blocks[block]->m_files[index];
}

UOPFile* UOPPackage::getFileByName(const std::string &filename)
{
    unsigned long long hash = hashFileName(filename);
    unsigned int block = kInvalidIdx, index = kInvalidIdx;
    if ( searchByHash(hash, block, index) )
        return getFileByIndex(block, index);
    return nullptr;
}

bool UOPPackage::searchByHash(unsigned long long hash, unsigned int& block, unsigned int& index) const
{
    unsigned int idx;
    for (unsigned int bl = 0, sz = unsigned(m_blocks.size()); bl < sz; ++bl)
    {
        idx = m_blocks[bl]->searchByHash(hash);
        if( idx != kInvalidIdx )
        {
            block = bl;
            index = idx;
            return true;
        }
    }
    return false;
}


//--

std::ifstream UOPPackage::getOpenedStream()
{
    std::ifstream fin;
    fin.open(m_packageName, std::ios::in | std::ios::binary );
    return fin;
}

bool UOPPackage::load(const std::string& fileName, UOPError* errorQueue)
{
    std::ifstream fin;
    fin.open(fileName, std::ios::in | std::ios::binary );
    if ( !fin.is_open() )
    {
        ADDERROR("Cannot open (read) ( " + fileName + ")");
        return false;
    }

    // Reading UOP Header
    char MYP0[4];

    fin.read(MYP0, 4);
    if ( MYP0[0] != 'M' || MYP0[1] != 'Y' || MYP0[2] != 'P' || MYP0[3] != 0 )
    {
        ADDERROR("Invalid Mythic Package file ( " + fileName + ")");
        return false;
    }

    fin.read(reinterpret_cast<char*>(&m_version), 4);
    if ( m_version > kMaxSupportedVersion )
    {
        ADDERROR("Unsupported Mythic Package version ( " + fileName + ")");
        return false;
    }

    m_packageName = fileName;

    fin.read(reinterpret_cast<char*>(&m_misc), 4);
    fin.read(reinterpret_cast<char*>(&m_startAddress), 8);  // address of the first block
    fin.read(reinterpret_cast<char*>(&m_blockSize), 4);
    fin.read(reinterpret_cast<char*>(&m_fileCount), 4);

    fin.seekg(std::streamoff(m_startAddress), std::ios::beg);

    // Read the blocks data inside the UOP file
    unsigned int index = 0;
    bool iseof = false;
    do
    {
        UOPBlock* b = new UOPBlock(this, index);
        b->read(fin, errorQueue);
        m_blocks.push_back(b);

        unsigned long long nextbl = b->getNextBlockAddress();

        if (nextbl == 0)
            iseof = true;

        fin.seekg(std::streamoff(nextbl), std::ios::beg);
        ++index;
    }
    while ( !iseof );

    fin.close();

    return true;
}


//--

bool UOPPackage::addFile(const std::string& filePath, unsigned long long fileHash, CompressionFlag compression, bool addDataHash, UOPError *errorQueue)
{
    std::stringstream ssHash; ssHash << std::hex << fileHash;
    std::string strHash("0x" + ssHash.str());
    if (fileHash == 0)
    {
        ADDERROR("Invalid fileHash for UOPPackage::addFile (" + strHash + ")");
        return false;
    }
    if (compression == CompressionFlag::Uninitialized)
    {
        ADDERROR("Invalid compression flag for UOPPackage::addFile: " + std::to_string(short(compression)) + " (" + strHash + ")");
        return false;
    }
    std::ifstream fin;
    fin.open(filePath, std::ios::in | std::ios::binary );
    if ( !fin.is_open() )
    {
        ADDERROR("Cannot open (read) " + filePath);
        return false;
    }

    UOPBlock* curBlock;
    if ( !m_blocks.empty() && (m_blocks[m_curBlockIdx]->getFilesCount() < m_blockSize))
        curBlock = m_blocks[m_curBlockIdx];
    else
    {
        curBlock = new UOPBlock(this, m_curBlockIdx);
        m_blocks.push_back(curBlock);
        m_curBlockIdx = unsigned(m_blocks.size()) - 1;
    }
    if (!curBlock->addFile(fin, fileHash, compression, addDataHash, errorQueue))
        return false;
    fin.close();
    return true;
}

bool UOPPackage::addFile(const std::string& filePath, const std::string& packedFileName, CompressionFlag compression, bool addDataHash, UOPError *errorQueue)
{
    if (packedFileName.empty())
    {
        ADDERROR("Invalid packedFileName for UOPPackage::addFile (" + packedFileName + ")");
        return false;
    }
    if (compression == CompressionFlag::Uninitialized)
    {
        ADDERROR("Invalid compression flag for UOPPackage::addFile: " + std::to_string(short(compression)) + " (" + packedFileName + ")");
        return false;
    }
    unsigned long long fileHash = hashFileName(packedFileName);
    return addFile(filePath, fileHash, compression, addDataHash, errorQueue);
}

bool UOPPackage::finalizeAndSave(const std::string& uopPath, UOPError* errorQueue)
{
    if (uopPath.empty())
    {
        ADDERROR("Invalid path for UOPPackage::save " + uopPath);
        return false;
    }
    std::ofstream fout;
    fout.open(uopPath, std::ios::out | std::ios::binary );
    if ( !fout.is_open() )
    {
        ADDERROR("Cannot open (write) " + m_packageName);
        return false;
    }

    // How many files do we have in the package?
    m_fileCount = 0;
    for (const auto& curFile : m_blocks)
    {
        m_fileCount += curFile->getFilesCount();
    }


    /* Start of the Header Data */

    // Write UOP file header
    static const unsigned int kPackageHeaderSize = 32;  // Size of the header in bytes = 4+4+4+8+4+4=32
    static const unsigned int kV5FirstBlockHeaderOffset = 0x200;

    const char MYP0[4] = {'M', 'Y', 'P', 0};
    fout.write(MYP0, 4);
    fout.write(reinterpret_cast<char*>(&m_version), 4);
    fout.write(reinterpret_cast<char*>(&m_misc), 4);

    if (m_version == 5)
    {
        // Add a zero-padding before the start of the header data
        //static const long long firstTableAddress = 0x200;
        m_startAddress = kV5FirstBlockHeaderOffset;
    }
    else
    {
        // No zero-padding (at least, version 4 doesn't have it, dunno for prior versions?)
        //m_startAddress = static_cast<unsigned long long>(fout.tellp()) + 8 + 4 + 4;
        m_startAddress = kPackageHeaderSize; // size of this header section: 4 + 4 + 8 + 4 + 4
    }
    fout.write(reinterpret_cast<char*>(&m_startAddress), 8);

    fout.write(reinterpret_cast<char*>(&m_blockSize), 4);
    fout.write(reinterpret_cast<char*>(&m_fileCount), 4);

    if (m_version == 5)
    {
        char paddingData[kV5FirstBlockHeaderOffset - kPackageHeaderSize] = {0};
        fout.write(paddingData, sizeof(paddingData));
    }

    // We'll need these addresses later
    std::vector<std::streamoff> blockInfoStartAddresses;
    blockInfoStartAddresses.reserve(m_blocks.size());
    std::vector<std::streamoff> fileInfoStartAddresses;
    fileInfoStartAddresses.reserve(m_blocks.size() * m_blockSize);

    // Loop through the blocks
    fout.seekp(std::streamoff(m_startAddress));
    for (const auto& curBlock : m_blocks)
    {
        blockInfoStartAddresses.push_back(fout.tellp());

        // Write UOP block header
        fout.write(reinterpret_cast<char*>(&curBlock->m_fileCount), 4);
        fout.write(reinterpret_cast<char*>(&curBlock->m_nextBlockAddress), 8);  // i don't have this yet, just write 0.
                                                                                // also 0 is the legit value if it's the last block
        unsigned int iFile = 0;
        // Loop through all the files of this block
        for (const auto& curFile : curBlock->m_files)
        {
            fileInfoStartAddresses.push_back(fout.tellp());

            // Write data for this single UOP file
            curFile->m_dataBlockAddress = 0;
            fout.write(reinterpret_cast<char*>(&curFile->m_dataBlockAddress), 8); // i don't have this yet, just write 0
            curFile->m_dataBlockLength = 0;
            fout.write(reinterpret_cast<char*>(&curFile->m_dataBlockLength), 4);
            fout.write(reinterpret_cast<char*>(&curFile->m_compressedSize), 4);
            fout.write(reinterpret_cast<char*>(&curFile->m_decompressedSize), 4);
            fout.write(reinterpret_cast<char*>(&curFile->m_fileHash), 8);
            fout.write(reinterpret_cast<char*>(&curFile->m_dataBlockHash), 4);
            fout.write(reinterpret_cast<char*>(&curFile->m_compression), 2);
            ++iFile;
        }

        // If we have a number of files in this block < of the blockSize, write empty headers.
        //  Each block must have a number of headers equal to blockSize.
        while (iFile < m_blockSize)
        {
            static const char emptyFileHeader[34] = {0};
            fout.write(emptyFileHeader, sizeof(emptyFileHeader));
            ++iFile;
        }
    }

    // Now update UOPBlock::m_nextBlockAddress for each block
    std::streampos endPackageHeader = fout.tellp();
    if (blockInfoStartAddresses.size() > 1)
    {
        for (size_t i = 0, sz = blockInfoStartAddresses.size() - 1; i < sz; ++i)
        {
            fout.seekp(std::streamoff(blockInfoStartAddresses[i]) + 4);
            fout.write(reinterpret_cast<char*>(&blockInfoStartAddresses[i + 1]), 8);
        }
    }
    fout.seekp(endPackageHeader);

    /* End of the Header Data */


    // Now write the actual file data separately from the info zone, and write back the m_dataBlockAddress in the file header
    unsigned int iFile = 0;
    for (const auto& curBlock : m_blocks)
    {
        for (const auto& curFile : curBlock->m_files)
        {
            // Write UOP file raw data
            unsigned long long beforeDataPos = static_cast<unsigned long long>(fout.tellp());
            fout.write( curFile->m_data.data(), std::streamsize(curFile->m_data.size()) );
            std::streampos afterDataPos = fout.tellp();

            // Write back m_dataBlockAddress
            fout.seekp(fileInfoStartAddresses[iFile]);
            fout.write(reinterpret_cast<char*>(&beforeDataPos), 8);
            fout.seekp(afterDataPos);

            ++iFile;
        }
    }

    // Done
    bool ret = !fout.bad();
    if (ret)
    {
        ADDERROR("Bad ofstream");
    }
    fout.close();
    return ret;
}


// Iterators

UOPPackage::iterator UOPPackage::end()       // past-the-end iterator (obtained when incrementing an iterator to the last item)
{
    return {this, kInvalidIdx, kInvalidIdx};
}

UOPPackage::iterator UOPPackage::begin()     // iterator to first item
{
    if (getBlocksCount() > 0)
    {
        if (getBlock(0)->getFilesCount() > 0)
            return {this, 0, 0};
    }
    return end();
}

UOPPackage::iterator UOPPackage::back_it()     // iterator to last item
{
    unsigned int blockCount = getBlocksCount();
    if (blockCount > 0)
    {
        unsigned int fileCount = getBlock(blockCount - 1)->getFilesCount();
        if (fileCount > 0)
            return {this, blockCount - 1, fileCount - 1};
    }
    return end();
}

UOPPackage::const_iterator UOPPackage::cend() const     // past-the-end iterator (obtained when incrementing an iterator to the last item)
{
    return {this, kInvalidIdx, kInvalidIdx};
}

UOPPackage::const_iterator UOPPackage::cbegin() const   // iterator to first item
{
    if (getBlocksCount() > 0)
    {
        if (getBlock(0)->getFilesCount() > 0)
            return {this, 0, 0};
    }
    return cend();
}

UOPPackage::const_iterator UOPPackage::cback_it() const // iterator to last item
{
    unsigned int blockCount = getBlocksCount();
    if (blockCount > 0)
    {
        unsigned int fileCount = getBlock(blockCount - 1)->getFilesCount();
        if (fileCount > 0)
            return {this, blockCount - 1, fileCount - 1};
    }
    return cend();
}


} // end of uopp namespace
