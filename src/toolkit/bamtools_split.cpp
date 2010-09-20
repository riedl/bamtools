// ***************************************************************************
// bamtools_split.cpp (c) 2010 Derek Barnett, Erik Garrison
// Marth Lab, Department of Biology, Boston College
// All rights reserved.
// ---------------------------------------------------------------------------
// Last modified: 19 September 2010 (DB)
// ---------------------------------------------------------------------------
// 
// ***************************************************************************

#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "bamtools_split.h"
#include "bamtools_options.h"
#include "bamtools_variant.h"
#include "BamReader.h"
#include "BamWriter.h"
using namespace std;
using namespace BamTools;

namespace BamTools {
  
    // string constants
    static const string SPLIT_MAPPED_TOKEN    = ".MAPPED";
    static const string SPLIT_UNMAPPED_TOKEN  = ".UNMAPPED";
    static const string SPLIT_PAIRED_TOKEN    = ".PAIRED_END";
    static const string SPLIT_SINGLE_TOKEN    = ".SINGLE_END";
    static const string SPLIT_REFERENCE_TOKEN = ".REF_";

    string GetTimestampString(void) {
      
        // get human readable timestamp
        time_t currentTime;
        time(&currentTime);
        stringstream timeStream("");
        timeStream << ctime(&currentTime);
        
        // convert whitespace to '_'
        string timeString = timeStream.str();
        size_t found = timeString.find(" ");
        while (found != string::npos) {
            timeString.replace(found, 1, "_");
            found = timeString.find(" ", found+1);
        }
        return timeString;
    }
    
    // remove copy of filename without extension 
    // (so /path/to/file.txt becomes /path/to/file )
    string RemoveFilenameExtension(const string& filename) {
        size_t found = filename.rfind(".");
        return filename.substr(0, found);
    }
    
} // namespace BamTools

// ---------------------------------------------
// SplitSettings implementation

struct SplitTool::SplitSettings {

    // flags
    bool HasInputFilename;
    bool HasCustomOutputStub;
    bool IsSplittingMapped;
    bool IsSplittingPaired;
    bool IsSplittingReference;
    bool IsSplittingTag;
    
    // string args
    string CustomOutputStub;
    string InputFilename;
    string TagToSplit;
    
    // constructor
    SplitSettings(void)
        : HasInputFilename(false)
        , HasCustomOutputStub(false)
        , IsSplittingMapped(false)
        , IsSplittingPaired(false)
        , IsSplittingReference(false)
        , IsSplittingTag(false)
        , CustomOutputStub("")
        , InputFilename(Options::StandardIn())
        , TagToSplit("")
    { } 
};  

// ---------------------------------------------
// SplitToolPrivate declaration

class SplitTool::SplitToolPrivate {
      
    // ctor & dtor
    public:
        SplitToolPrivate(SplitTool::SplitSettings* settings);
        ~SplitToolPrivate(void);
        
    // 'public' interface
    public:
        bool Run(void);
        
    // internal methods
    private:
        void DetermineOutputFilenameStub(void);
        bool OpenReader(void);
        bool SplitMapped(void);
        bool SplitPaired(void);
        bool SplitReference(void);
        bool SplitTag(void);
        bool SplitTag_Int(BamAlignment& al);
        bool SplitTag_UInt(BamAlignment& al);
        bool SplitTag_Real(BamAlignment& al);
        bool SplitTag_String(BamAlignment& al);
        
    // data members
    private:
        SplitTool::SplitSettings* m_settings;
        string m_outputFilenameStub;
        BamReader m_reader;
        string m_header;
        RefVector m_references;
};

// constructor
SplitTool::SplitToolPrivate::SplitToolPrivate(SplitTool::SplitSettings* settings) 
    : m_settings(settings)
{ }

// destructor
SplitTool::SplitToolPrivate::~SplitToolPrivate(void) { 
    m_reader.Close();
}

void SplitTool::SplitToolPrivate::DetermineOutputFilenameStub(void) {
  
    // if user supplied output filename stub, use that
    if ( m_settings->HasCustomOutputStub ) 
        m_outputFilenameStub = m_settings->CustomOutputStub;
    
    // else if user supplied input BAM filename, use that (minus ".bam" extension) as stub
    else if ( m_settings->HasInputFilename )
        m_outputFilenameStub = RemoveFilenameExtension(m_settings->InputFilename);
        
    // otherwise, user did not specify -stub, and input is coming from STDIN
    // generate stub from timestamp
    else m_outputFilenameStub = GetTimestampString();      
}

bool SplitTool::SplitToolPrivate::OpenReader(void) {
    if ( !m_reader.Open(m_settings->InputFilename) ) {
        cerr << "ERROR: SplitTool could not open BAM file: " << m_settings->InputFilename << endl;
        return false;
    }
    m_header     = m_reader.GetHeaderText();
    m_references = m_reader.GetReferenceData();
    return true;
}

bool SplitTool::SplitToolPrivate::Run(void) {
  
    // determine output stub
    DetermineOutputFilenameStub();

    // open up BamReader
    if ( !OpenReader() ) return false;
    
    // determine split type from settings
    if ( m_settings->IsSplittingMapped )    return SplitMapped();
    if ( m_settings->IsSplittingPaired )    return SplitPaired();
    if ( m_settings->IsSplittingReference ) return SplitReference();
    if ( m_settings->IsSplittingTag )       return SplitTag();

    // if we get here, no property was specified 
    cerr << "No property given to split on... Please use -mapped, -paired, -reference, or -tag TAG to specifiy split behavior." << endl;
    return false;
}    

bool SplitTool::SplitToolPrivate::SplitMapped(void) {
    
    // set up splitting data structure
    map<bool, BamWriter*> outputFiles;
    map<bool, BamWriter*>::iterator writerIter;
    
    // iterate through alignments
    BamAlignment al;
    BamWriter* writer;
    bool isCurrentAlignmentMapped;
    while ( m_reader.GetNextAlignment(al) ) {
      
        // see if bool value exists
        isCurrentAlignmentMapped = al.IsMapped();
        writerIter = outputFiles.find(isCurrentAlignmentMapped);
          
        // if no writer associated with this value
        if ( writerIter == outputFiles.end() ) {
        
            // open new BamWriter
            writer = new BamWriter;
            const string outputFilename = m_outputFilenameStub + ( isCurrentAlignmentMapped ? SPLIT_MAPPED_TOKEN : SPLIT_UNMAPPED_TOKEN ) + ".bam";
            writer->Open(outputFilename, m_header, m_references);
          
            // store in map
            outputFiles.insert( make_pair(isCurrentAlignmentMapped, writer) );
        } 
        
        // else grab corresponding writer
        else writer = (*writerIter).second;
        
        // store alignment in proper BAM output file 
        if ( writer ) 
            writer->SaveAlignment(al);
    }
    
    // clean up BamWriters 
    map<bool, BamWriter*>::iterator writerEnd  = outputFiles.end();
    for ( writerIter = outputFiles.begin() ; writerIter != writerEnd; ++writerIter ) {
        BamWriter* writer = (*writerIter).second;
        if ( writer == 0 ) continue;
        writer->Close();
        delete writer;
        writer = 0;
    }
    
    // return success
    return true;
}

bool SplitTool::SplitToolPrivate::SplitPaired(void) {
  
    // set up splitting data structure
    map<bool, BamWriter*> outputFiles;
    map<bool, BamWriter*>::iterator writerIter;
    
    // iterate through alignments
    BamAlignment al;
    BamWriter* writer;
    bool isCurrentAlignmentPaired;
    while ( m_reader.GetNextAlignment(al) ) {
      
        // see if bool value exists
        isCurrentAlignmentPaired = al.IsPaired();
        writerIter = outputFiles.find(isCurrentAlignmentPaired);
          
        // if no writer associated with this value
        if ( writerIter == outputFiles.end() ) {
        
            // open new BamWriter
            writer = new BamWriter;
            const string outputFilename = m_outputFilenameStub + ( isCurrentAlignmentPaired ? SPLIT_PAIRED_TOKEN : SPLIT_SINGLE_TOKEN ) + ".bam";
            writer->Open(outputFilename, m_header, m_references);
          
            // store in map
            outputFiles.insert( make_pair(isCurrentAlignmentPaired, writer) );
        } 
        
        // else grab corresponding writer
        else writer = (*writerIter).second;
        
        // store alignment in proper BAM output file 
        if ( writer ) 
            writer->SaveAlignment(al);
    }
    
    // clean up BamWriters 
    map<bool, BamWriter*>::iterator writerEnd  = outputFiles.end();
    for ( writerIter = outputFiles.begin() ; writerIter != writerEnd; ++writerIter ) {
        BamWriter* writer = (*writerIter).second;
        if (writer == 0 ) continue;
        writer->Close();
        delete writer;
        writer = 0;
    }
    
    // return success
    return true;  
}

bool SplitTool::SplitToolPrivate::SplitReference(void) {
  
    // set up splitting data structure
    map<int32_t, BamWriter*> outputFiles;
    map<int32_t, BamWriter*>::iterator writerIter;
    
    // iterate through alignments
    BamAlignment al;
    BamWriter* writer;
    int32_t currentRefId;
    while ( m_reader.GetNextAlignment(al) ) {
      
        // see if bool value exists
        currentRefId = al.RefID;
        writerIter = outputFiles.find(currentRefId);
          
        // if no writer associated with this value
        if ( writerIter == outputFiles.end() ) {
        
            // open new BamWriter
            writer = new BamWriter;
            const string refName = m_references.at(currentRefId).RefName;
            const string outputFilename = m_outputFilenameStub + SPLIT_REFERENCE_TOKEN + refName + ".bam";
            writer->Open(outputFilename, m_header, m_references);
          
            // store in map
            outputFiles.insert( make_pair(currentRefId, writer) );
        } 
        
        // else grab corresponding writer
        else writer = (*writerIter).second;
        
        // store alignment in proper BAM output file 
        if ( writer ) 
            writer->SaveAlignment(al);
    }
    
    // clean up BamWriters 
    map<int32_t, BamWriter*>::iterator writerEnd  = outputFiles.end();
    for ( writerIter = outputFiles.begin(); writerIter != writerEnd; ++writerIter ) {
        BamWriter* writer = (*writerIter).second;
        if (writer == 0 ) continue;
        writer->Close();
        delete writer;
        writer = 0;
    }
    
    // return success
    return true;
}

bool SplitTool::SplitToolPrivate::SplitTag(void) {  
  
    // iterate through alignments, until we hit TAG
    BamAlignment al;
    while ( m_reader.GetNextAlignment(al) ) {
      
        // look for tag in this alignment and get tag type
        char tagType(0);
        if ( !al.GetTagType(m_settings->TagToSplit, tagType) ) continue;
        
        // request split method based on tag type
        // pass it the current alignment found
        switch (tagType) {
          
            case 'c' :
            case 's' : 
            case 'i' :
                return SplitTag_Int(al);
                
            case 'C' :
            case 'S' :
            case 'I' : 
                return SplitTag_UInt(al);
              
            case 'f' :
                return SplitTag_Real(al);
            
            case 'A':
            case 'Z':
            case 'H':
                return SplitTag_String(al);
          
            default:
                fprintf(stderr, "ERROR: Unknown tag storage class encountered: [%c]\n", tagType);
                return false;
        }
    }
    
    // tag not found, but that's not an error - return success
    return true;
}

bool SplitTool::SplitToolPrivate::SplitTag_Int(BamAlignment& al) {
  
    // set up splitting data structure
    map<int32_t, BamWriter*> outputFiles;
    map<int32_t, BamWriter*>::iterator writerIter;

    // local variables
    const string tag = m_settings->TagToSplit;
    BamWriter* writer;
    stringstream outputFilenameStream("");
    int32_t currentValue;
    
    // retrieve first alignment tag value
    if ( al.GetTag(tag, currentValue) ) {
      
        // open new BamWriter, save first alignment
        writer = new BamWriter;
        outputFilenameStream << m_outputFilenameStub << ".TAG_" << tag << "_" << currentValue << ".bam";
        writer->Open(outputFilenameStream.str(), m_header, m_references);
        writer->SaveAlignment(al);
        
        // store in map
        outputFiles.insert( make_pair(currentValue, writer) );
        
        // reset stream
        outputFilenameStream.str("");
    }
    
    // iterate through remaining alignments
    while ( m_reader.GetNextAlignment(al) ) {
      
        // skip if this alignment doesn't have TAG 
        if ( !al.GetTag(tag, currentValue) ) continue;
        
        // look up tag value in map
        writerIter = outputFiles.find(currentValue);
          
        // if no writer associated with this value
        if ( writerIter == outputFiles.end() ) {
        
            // open new BamWriter
            writer = new BamWriter;
            outputFilenameStream << m_outputFilenameStub << ".TAG_" << tag << "_" << currentValue << ".bam";
            writer->Open(outputFilenameStream.str(), m_header, m_references);
            
            // store in map
            outputFiles.insert( make_pair(currentValue, writer) );
            
            // reset stream
            outputFilenameStream.str("");
        } 
        
        // else grab corresponding writer
        else writer = (*writerIter).second;
        
        // store alignment in proper BAM output file 
        if ( writer ) 
            writer->SaveAlignment(al);
    }
    
    // clean up BamWriters 
    map<int32_t, BamWriter*>::iterator writerEnd  = outputFiles.end();
    for ( writerIter = outputFiles.begin(); writerIter != writerEnd; ++writerIter ) {
        BamWriter* writer = (*writerIter).second;
        if (writer == 0 ) continue;
        writer->Close();
        delete writer;
        writer = 0;
    }
    
    // return success
    return true;
}

bool SplitTool::SplitToolPrivate::SplitTag_UInt(BamAlignment& al) {
  
    // set up splitting data structure
    map<uint32_t, BamWriter*> outputFiles;
    map<uint32_t, BamWriter*>::iterator writerIter;

    // local variables
    const string tag = m_settings->TagToSplit;
    BamWriter* writer;
    stringstream outputFilenameStream("");
    uint32_t currentValue;
    
    int alignmentsIgnored = 0;
    
    // retrieve first alignment tag value
    if ( al.GetTag(tag, currentValue) ) {
      
        // open new BamWriter, save first alignment
        writer = new BamWriter;
        outputFilenameStream << m_outputFilenameStub << ".TAG_" << tag << "_" << currentValue << ".bam";
        writer->Open(outputFilenameStream.str(), m_header, m_references);
        writer->SaveAlignment(al);
        
        // store in map
        outputFiles.insert( make_pair(currentValue, writer) );
        
        // reset stream
        outputFilenameStream.str("");
    } else ++alignmentsIgnored;
    
    // iterate through remaining alignments
    while ( m_reader.GetNextAlignment(al) ) {
      
        // skip if this alignment doesn't have TAG 
        if ( !al.GetTag(tag, currentValue) ) { ++alignmentsIgnored; continue; }
        
        // look up tag value in map
        writerIter = outputFiles.find(currentValue);
          
        // if no writer associated with this value
        if ( writerIter == outputFiles.end() ) {
        
            // open new BamWriter
            writer = new BamWriter;
            outputFilenameStream << m_outputFilenameStub << ".TAG_" << tag << "_" << currentValue << ".bam";
            writer->Open(outputFilenameStream.str(), m_header, m_references);
            
            // store in map
            outputFiles.insert( make_pair(currentValue, writer) );
            
            // reset stream
            outputFilenameStream.str("");
        } 
        
        // else grab corresponding writer
        else writer = (*writerIter).second;
        
        // store alignment in proper BAM output file 
        if ( writer ) 
            writer->SaveAlignment(al);
    }
    
    // clean up BamWriters 
    map<uint32_t, BamWriter*>::iterator writerEnd  = outputFiles.end();
    for ( writerIter = outputFiles.begin(); writerIter != writerEnd; ++writerIter ) {
        BamWriter* writer = (*writerIter).second;
        if (writer == 0 ) continue;
        writer->Close();
        delete writer;
        writer = 0;
    }
    
    // return success
    return true;
}

bool SplitTool::SplitToolPrivate::SplitTag_Real(BamAlignment& al) {

     // set up splitting data structure
    map<float, BamWriter*> outputFiles;
    map<float, BamWriter*>::iterator writerIter;

    // local variables
    const string tag = m_settings->TagToSplit;
    BamWriter* writer;
    stringstream outputFilenameStream("");
    float currentValue;
    
    // retrieve first alignment tag value
    if ( al.GetTag(tag, currentValue) ) {
      
        // open new BamWriter, save first alignment
        writer = new BamWriter;
        outputFilenameStream << m_outputFilenameStub << ".TAG_" << tag << "_" << currentValue << ".bam";
        writer->Open(outputFilenameStream.str(), m_header, m_references);
        writer->SaveAlignment(al);
        
        // store in map
        outputFiles.insert( make_pair(currentValue, writer) );
        
        // reset stream
        outputFilenameStream.str("");
    }
    
    // iterate through remaining alignments
    while ( m_reader.GetNextAlignment(al) ) {
      
        // skip if this alignment doesn't have TAG 
        if ( !al.GetTag(tag, currentValue) ) continue;
        
        // look up tag value in map
        writerIter = outputFiles.find(currentValue);
          
        // if no writer associated with this value
        if ( writerIter == outputFiles.end() ) {
        
            // open new BamWriter
            writer = new BamWriter;
            outputFilenameStream << m_outputFilenameStub << ".TAG_" << tag << "_" << currentValue << ".bam";
            writer->Open(outputFilenameStream.str(), m_header, m_references);
            
            // store in map
            outputFiles.insert( make_pair(currentValue, writer) );
            
            // reset stream
            outputFilenameStream.str("");
        } 
        
        // else grab corresponding writer
        else writer = (*writerIter).second;
        
        // store alignment in proper BAM output file 
        if ( writer ) 
            writer->SaveAlignment(al);
    }
    
    // clean up BamWriters 
    map<float, BamWriter*>::iterator writerEnd  = outputFiles.end();
    for ( writerIter = outputFiles.begin(); writerIter != writerEnd; ++writerIter ) {
        BamWriter* writer = (*writerIter).second;
        if (writer == 0 ) continue;
        writer->Close();
        delete writer;
        writer = 0;
    }
    
    // return success
    return true;
}

bool SplitTool::SplitToolPrivate::SplitTag_String(BamAlignment& al) {
  
     // set up splitting data structure
    map<string, BamWriter*> outputFiles;
    map<string, BamWriter*>::iterator writerIter;

    // local variables
    const string tag = m_settings->TagToSplit;
    BamWriter* writer;
    stringstream outputFilenameStream("");
    string currentValue;
    
    // retrieve first alignment tag value
    if ( al.GetTag(tag, currentValue) ) {
      
        // open new BamWriter, save first alignment
        writer = new BamWriter;
        outputFilenameStream << m_outputFilenameStub << ".TAG_" << tag << "_" << currentValue << ".bam";
        writer->Open(outputFilenameStream.str(), m_header, m_references);
        writer->SaveAlignment(al);
        
        // store in map
        outputFiles.insert( make_pair(currentValue, writer) );
        
        // reset stream
        outputFilenameStream.str("");
    }
    
    // iterate through remaining alignments
    while ( m_reader.GetNextAlignment(al) ) {
      
        // skip if this alignment doesn't have TAG 
        if ( !al.GetTag(tag, currentValue) ) continue;
        
        // look up tag value in map
        writerIter = outputFiles.find(currentValue);
          
        // if no writer associated with this value
        if ( writerIter == outputFiles.end() ) {
        
            // open new BamWriter
            writer = new BamWriter;
            outputFilenameStream << m_outputFilenameStub << ".TAG_" << tag << "_" << currentValue << ".bam";
            writer->Open(outputFilenameStream.str(), m_header, m_references);
            
            // store in map
            outputFiles.insert( make_pair(currentValue, writer) );
            
            // reset stream
            outputFilenameStream.str("");
        } 
        
        // else grab corresponding writer
        else writer = (*writerIter).second;
        
        // store alignment in proper BAM output file 
        if ( writer ) 
            writer->SaveAlignment(al);
    }
    
    // clean up BamWriters 
    map<string, BamWriter*>::iterator writerEnd  = outputFiles.end();
    for ( writerIter = outputFiles.begin(); writerIter != writerEnd; ++writerIter ) {
        BamWriter* writer = (*writerIter).second;
        if (writer == 0 ) continue;
        writer->Close();
        delete writer;
        writer = 0;
    }
    
    // return success
    return true;
}

// ---------------------------------------------
// SplitTool implementation

SplitTool::SplitTool(void)
    : AbstractTool()
    , m_settings(new SplitSettings)
    , m_impl(0)
{
    // set program details
    Options::SetProgramInfo("bamtools split", "splits a BAM file on user-specified property, creating a new BAM output file for each value found", "[-in <filename>] [-stub <filename>] < -mapped | -paired | -reference | -tag <TAG> > ");
    
    // set up options 
    OptionGroup* IO_Opts = Options::CreateOptionGroup("Input & Output");
    Options::AddValueOption("-in",  "BAM filename", "the input BAM file",  "", m_settings->HasInputFilename,  m_settings->InputFilename,  IO_Opts, Options::StandardIn());
    Options::AddValueOption("-stub", "filename stub", "prefix stub for output BAM files (default behavior is to use input filename, without .bam extension, as stub). If input is stdin and no stub provided, a timestamp is generated as the stub.", "", m_settings->HasCustomOutputStub, m_settings->CustomOutputStub, IO_Opts);
    
    OptionGroup* SplitOpts = Options::CreateOptionGroup("Split Options");
    Options::AddOption("-mapped",    "split mapped/unmapped alignments",       m_settings->IsSplittingMapped,    SplitOpts);
    Options::AddOption("-paired",    "split single-end/paired-end alignments", m_settings->IsSplittingPaired,    SplitOpts);
    Options::AddOption("-reference", "split alignments by reference",          m_settings->IsSplittingReference, SplitOpts);
    Options::AddValueOption("-tag", "tag name", "splits alignments based on all values of TAG encountered (i.e. -tag RG creates a BAM file for each read group in original BAM file)", "", 
                            m_settings->IsSplittingTag, m_settings->TagToSplit, SplitOpts);
}

SplitTool::~SplitTool(void) {
    
    delete m_settings;
    m_settings = 0;
    
    delete m_impl;
    m_impl = 0;
}

int SplitTool::Help(void) {
    Options::DisplayHelp();
    return 0;
}

int SplitTool::Run(int argc, char* argv[]) {
  
    // parse command line arguments
    Options::Parse(argc, argv, 1);
    
    // initialize internal implementation
    m_impl = new SplitToolPrivate(m_settings);
    
    // run tool, return success/fail
    if ( m_impl->Run() ) 
        return 0;
    else 
        return 1;
}