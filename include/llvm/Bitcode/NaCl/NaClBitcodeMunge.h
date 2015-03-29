//===- NaClBitcodeMunge.h - Bitcode Munger ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Test harness for generating a PNaCl bitcode memory buffer from
// an array, and objdump the resulting contents.
//
// Generates a bitcode memory buffer from an array containing 1 or
// more PNaCl records. Used to test errors in PNaCl bitcode.
//
// For simplicity of API, we model records as an array of uint64_t. To
// specify the end of a record in the array, a special "terminator"
// value is used. This terminator value must be a uint64_t value that
// is not used by any of the records.
//
// A record is defined as a sequence of integers consisting of:
//   * The abbreviation index associated with the record.
//   * The record code associated with the record.
//   * The sequence of values associated with the record code.
//   * The terminator value.
//
// Note: Since the header record doesn't have any abbreviation indices
// associated with it, one can use any value. The value will simply be
// ignored.
//
// In addition to specifying the sequence of records, one can also
// define a sequence of edits to be applied to the original sequence
// of records. This allows the same record sequence to be used in
// multiple tests.
//
// An edit consists of:
//
//   * A record number defining where the edit is to be applied.
//   * An action defining the type of edit.
//   * An optional record associated with the action.
//
// Note that edits must be sorted by record number, so that the records
// and edits can be processed with a linear pass.
//
// Generally, you can generate any legal/illegal record
// sequence. However, abbreviations are intimately tied to the
// internals of the bitstream writer and can't contain illegal
// data. Whenever class NaClBitcodeMunger is unable to accept illegal
// data, a corresponding "Fatal" error is generated and execution
// is terminated.
//
// ===---------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_NACL_NACLBITCODEMUNGE_H
#define LLVM_BITCODE_NACL_NACLBITCODEMUNGE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

namespace llvm {

class NaClBitstreamWriter;
class NaClBitCodeAbbrev;

/// Class to run tests for function llvm::NaClObjDump.
class NaClBitcodeMunger {
public:
  /// The types of editing actions that can be applied.
  enum EditAction {
    AddBefore, // Insert record before record with index.
    AddAfter,  // Insert record after record with index.
    Remove,    // Remove record with index.
    Replace    // Replace record with index with specified record.
  };

  /// Creates a bitcode munger, based on the given array of values.
  NaClBitcodeMunger(const uint64_t Records[], size_t RecordsSize,
                    uint64_t RecordTerminator)
      : Records(Records), RecordsSize(RecordsSize),
        RecordTerminator(RecordTerminator), WriteBlockID(-1), SetBID(-1),
        DumpResults("Error: No previous dump results!\n"),
        DumpStream(NULL), Writer(NULL), FoundErrors(false),
        FatalBuffer(), FatalStream(FatalBuffer) {}

  /// Runs function NaClObjDump on the sequence of records associated
  /// with the instance. The memory buffer containing the bitsequence
  /// associated with the record is automatically generated, and
  /// passed to NaClObjDump.  TestName is the name associated with the
  /// memory buffer.  If AddHeader is true, test assumes that the
  /// sequence of records doesn't contain a header record, and the
  /// test should add one. Arguments NoRecords and NoAssembly are
  /// passed to NaClObjDump. Returns true if test succeeds without
  /// errors.
  bool runTestWithFlags(const char *TestName, bool AddHeader,
                        bool NoRecords, bool NoAssembly) {
    uint64_t NoMunges[] = {0};
    return runTestWithFlags(TestName, NoMunges, 0, AddHeader, NoRecords,
                            NoAssembly);
  }

  /// Same as above except it runs function NaClObjDump with flags
  /// NoRecords and NoAssembly set to false, and AddHeader set to true.
  bool runTest(const char *TestName) {
    return runTestWithFlags(TestName, true, false, false);
  }

  /// Same as runTest, but only print out assembly and errors.
  bool runTestForAssembly(const char *TestName) {
    return runTestWithFlags(TestName, true, true, false);
  }

  /// Same as runTest, but only generate error messages.
  bool runTestForErrors(const char *TestName) {
    return runTestWithFlags(TestName, true, true, true);
  }

  /// Runs function llvm::NaClObjDump on the sequence of records
  /// associated with the instance. Array Munges contains the sequence
  /// of edits to apply to the sequence of records when generating the
  /// bitsequence in a memory buffer. This generated bitsequence is
  /// then passed to NaClObjDump.  TestName is the name associated
  /// with the memory buffer.  Arguments NoRecords and NoAssembly are
  /// passed to NaClObjDump. Returns true if test succeeds without
  /// errors.
  bool runTestWithFlags(const char* TestName, const uint64_t Munges[],
                        size_t MungesSize, bool AddHeader,
                        bool NoRecords, bool NoAssembly);

  /// Same as above except it runs function NaClObjDump with flags
  /// NoRecords and NoAssembly set to false, and AddHeader set to
  /// true.
  bool runTest(const char* TestName, const uint64_t Munges[],
                      size_t MungesSize) {
    return runTestWithFlags(TestName, Munges, MungesSize, true, false, false);
  }

  bool runTestForAssembly(const char* TestName, const uint64_t Munges[],
                          size_t MungesSize) {
    return runTestWithFlags(TestName, Munges, MungesSize, true, true, false);
  }

  bool runTestForErrors(const char* TestName, const uint64_t Munges[],
                        size_t MungesSize) {
    return runTestWithFlags(TestName, Munges, MungesSize, true, true, true);
  }

  /// Returns the resulting string generated by NaClObjDump.
  const std::string &getTestResults() const {
    return DumpResults;
  }

  /// Returns the lines containing the given Substring, from the
  /// string getTestResults().
  std::string getLinesWithSubstring(const std::string &Substring) const {
    return getLinesWithTextMatch(Substring, false);
  }

  /// Returns the lines starting with the given Prefix, from the string
  /// getTestResults().
  std::string getLinesWithPrefix(const std::string &Prefix) const {
    return getLinesWithTextMatch(Prefix, true);
  }

private:
  // The list of (base) records.
  const uint64_t *Records;
  // The number of (base) records.
  size_t RecordsSize;
  // The value used as record terminator.
  uint64_t RecordTerminator;
  // The block ID associated with the block being written.
  int WriteBlockID;
  // The SetBID for the blockinfo block.
  int SetBID;
  // The results buffer of the last dump.
  std::string DumpResults;
  // The stream containing errors and the objdump of the generated bitcode file.
  raw_ostream *DumpStream;
  // The bitstream writer to use to generate the bitcode file.
  NaClBitstreamWriter *Writer;
  // True if any errors were reported.
  bool FoundErrors;
  // The buffer to hold the generated fatal message.
  std::string FatalBuffer;
  // The stream to write the fatal message to.
  raw_string_ostream FatalStream;

  // Records that an error occurred, and returns stream to print error
  // message to.
  raw_ostream &Error() {
    FoundErrors = true;
    return *DumpStream << "Error: ";
  }

  // Returns stream to print fatal error message to.
  // Note: Once the fatal error message has been dumped to the stream,
  // one must call ReportFatalError to display the error and terminate
  // execution.
  raw_ostream &Fatal() {
    return FatalStream << "Fatal: ";
  }

  // Displays the fatal error message and exits the program.
  void ReportFatalError() {
    report_fatal_error(FatalStream.str());
  }

  /// Returns the lines containing the given Substring, from the
  /// string getTestResults(). If MustBePrefix, then Substring must
  /// match at the beginning of the line.
  std::string getLinesWithTextMatch(const std::string &Substring,
                                    bool MustBePrefix = false) const;

  // Writes out sequence of records, applying the given sequence of
  // munges.
  void writeMungedData(const uint64_t Munges[], size_t MungesSize,
                       bool AddHeader);

  // Emits the given record to the bitcode file.
  void emitRecord(unsigned AbbrevIndex, unsigned RecordCode,
                  SmallVectorImpl<uint64_t> &Values);

  // Converts the abbreviation record to the corresponding abbreviation.
  NaClBitCodeAbbrev *buildAbbrev(unsigned RecordCode,
                                 SmallVectorImpl<uint64_t> &Values);

  // Emits the record at the given Index to the bitcode file.  Then
  // updates Index by one.
  void emitRecordAtIndex(const uint64_t Record[], size_t RecordSize,
                         size_t &Index);

  // Skips the record starting at Index by advancing Index past the
  // contents of the record.
  void deleteRecord(const uint64_t Record[], size_t RecordSize,
                    size_t &Index);
};

} // end namespace llvm.

#endif // LLVM_BITCODE_NACL_NACLBITCODEMUNGE_H
