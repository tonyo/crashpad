// Copyright 2014 The Crashpad Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CRASHPAD_UTIL_MAC_MACH_O_IMAGE_READER_H_
#define CRASHPAD_UTIL_MAC_MACH_O_IMAGE_READER_H_

#include <mach/mach.h>
#include <stdint.h>

#include <map>
#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "util/misc/initialization_state_dcheck.h"
#include "util/misc/uuid.h"
#include "util/stdlib/pointer_container.h"
#include "util/mac/process_types.h"

namespace crashpad {

class MachOImageSegmentReader;
class ProcessReader;

//! \brief A reader for Mach-O images mapped into another process.
//!
//! This class is capable of reading both 32-bit (`mach_header`/`MH_MAGIC`) and
//! 64-bit (`mach_header_64`/`MH_MAGIC_64`) images based on the bitness of the
//! remote process.
class MachOImageReader {
 public:
  MachOImageReader();
  ~MachOImageReader();

  //! \brief Reads the Mach-O image file’s load commands from another process.
  //!
  //! This method must only be called once on an object. This method must be
  //! called successfully before any other method in this class may be called.
  //!
  //! \param[in] process_reader The reader for the remote process.
  //! \param[in] address The address, in the remote process’ address space,
  //!     where the `mach_header` or `mach_header_64` at the beginning of the
  //!     image to be read is located. This address can be determined by reading
  //!     the remote process’ dyld information (see
  //!     util/mac/process_types/dyld_images.proctype).
  //! \param[in] name The module’s name, a string to be used in logged messages.
  //!     This string is for diagnostic purposes only, and may be empty.
  //!
  //! \return `true` if the image was read successfully, including all load
  //!     commands. `false` otherwise, with an appropriate message logged.
  bool Initialize(ProcessReader* process_reader,
                  mach_vm_address_t address,
                  const std::string& name);

  //! \brief Returns the Mach-O file type.
  //!
  //! This value comes from the `filetype` field of the `mach_header` or
  //! `mach_header_64`. Common values include `MH_EXECUTE`, `MH_DYLIB`,
  //! `MH_DYLINKER`, and `MH_BUNDLE`.
  uint32_t FileType() const { return file_type_; }

  //! \brief Returns the Mach-O image’s load address.
  //!
  //! This is the value passed as \a address to Initialize().
  mach_vm_address_t Address() const { return address_; }

  //! \brief Returns the mapped size of the Mach-O image’s __TEXT segment.
  //!
  //! Note that this is returns only the size of the __TEXT segment, not of any
  //! other segment. This is because the interface only allows one load address
  //! and size to be reported, but Mach-O image files may consist of multiple
  //! discontiguous segments. By convention, the __TEXT segment is always mapped
  //! at the beginning of a Mach-O image file, and it is the most useful for the
  //! expected intended purpose of collecting data to obtain stack backtraces.
  //! The implementation insists during initialization that the __TEXT segment
  //! be mapped at the beginning of the file.
  //!
  //! In practice, discontiguous segments are only found for images that have
  //! loaded out of the dyld shared cache, but the __TEXT segment’s size is
  //! returned for modules that loaded with contiguous segments as well for
  //! consistency.
  mach_vm_size_t Size() const { return size_; }

  //! \brief Returns the Mach-O image’s “slide,” the difference between its
  //!     actual load address and its preferred load address.
  //!
  //! “Slide” is computed by subtracting the __TEXT segment’s preferred load
  //! address from its actual load address. It will be reported as a positive
  //! offset when the actual load address is greater than the preferred load
  //! address. The preferred load address is taken to be the segment’s reported
  //! `vmaddr` value.
  mach_vm_size_t Slide() const { return slide_; }

  //! \brief Obtain segment information by segment name.
  //!
  //! \param[in] segment_name The name of the segment to search for, for
  //!     example, `"__TEXT"`.
  //! \param[out] address The actual address that the segment was loaded at in
  //!     memory, taking any “slide” into account if the segment did not load at
  //!     its preferred address as stored in the Mach-O image file. This
  //!     parameter can be `NULL`.
  //! \param[out] size The actual size of the segment as loaded at in memory.
  //!     This value takes any expansion of the segment into account, which
  //!     occurs when a nonsliding segment in a sliding image loads at its
  //!     preferred address but grows by the value of the slide. This parameter
  //!     can be `NULL`.
  //!
  //! \return A pointer to the segment information if it was found, or `NULL` if
  //!     it was not found.
  //!
  //! \note The \a address parameter takes “slide” into account, and the \a size
  //!     parameter takes growth into account for non-sliding segments, so that
  //!     these parameters reflect the actual address and size of the segment as
  //!     loaded into a process’ address space. This is distinct from the
  //!     segment’s preferred load address and size, which may be obtained by
  //!     calling MachOImageSegmentReader::vmaddr() and
  //!     MachOImageSegmentReader::vmsize(), respectively.
  const MachOImageSegmentReader* GetSegmentByName(
      const std::string& segment_name,
      mach_vm_address_t* address,
      mach_vm_size_t* size) const;

  //! \brief Obtain section information by segment and section name.
  //!
  //! \param[in] segment_name The name of the segment to search for, for
  //!     example, `"__TEXT"`.
  //! \param[in] section_name The name of the section within the segment to
  //!     search for, for example, `"__text"`.
  //! \param[out] address The actual address that the section was loaded at in
  //!     memory, taking any “slide” into account if the section did not load at
  //!     its preferred address as stored in the Mach-O image file. This
  //!     parameter can be `NULL`.
  //!
  //! \return A pointer to the section information if it was found, or `NULL` if
  //!     it was not found.
  //!
  //! No parameter is provided for the section’s size, because it can be
  //! obtained from the returned process_types::section::size field.
  //!
  //! \note The process_types::section::addr field gives the section’s preferred
  //!     load address as stored in the Mach-O image file, and is not adjusted
  //!     for any “slide” that may have occurred when the image was loaded. Use
  //!     \a address to obtain the section’s actual load address.
  const process_types::section* GetSectionByName(
      const std::string& segment_name,
      const std::string& section_name,
      mach_vm_address_t* address) const;

  //! \brief Obtain section information by section index.
  //!
  //! \param[in] index The index of the section to return, in the order that it
  //!     appears in the segment load commands. This is a 1-based index,
  //!     matching the section number values used for `nlist::n_sect`.
  //! \param[out] address The actual address that the section was loaded at in
  //!     memory, taking any “slide” into account if the section did not load at
  //!     its preferred address as stored in the Mach-O image file. This
  //!     parameter can be `NULL`.
  //!
  //! \return A pointer to the section information. If \a index is out of range,
  //!     logs a warning and returns `NULL`.
  //!
  //! No parameter is provided for the section’s size, because it can be
  //! obtained from the returned process_types::section::size field.
  //!
  //! \note The process_types::section::addr field gives the section’s preferred
  //!     load address as stored in the Mach-O image file, and is not adjusted
  //!     for any “slide” that may have occurred when the image was loaded. Use
  //!     \a address to obtain the section’s actual load address.
  //! \note Unlike MachOImageSegmentReader::GetSectionAtIndex(), this method
  //!     accepts out-of-range values for \a index, and returns `NULL` instead
  //!     of aborting execution upon encountering an out-of-range value. This is
  //!     because a Mach-O image file’s symbol table refers to this per-module
  //!     section index, and an out-of-range index in that case should be
  //!     treated as a data error (where the data is beyond this code’s control)
  //!     and handled non-fatally by reporting the error to the caller.
  const process_types::section* GetSectionAtIndex(
      size_t index,
      mach_vm_address_t* address) const;

  //! \brief Returns a Mach-O dylib image’s current version.
  //!
  //! This information comes from the `dylib_current_version` field of a dylib’s
  //! `LC_ID_DYLIB` load command. For dylibs without this load command, `0` will
  //! be returned.
  //!
  //! This method may only be called on Mach-O images for which FileType()
  //! returns `MH_DYLIB`.
  uint32_t DylibVersion() const;

  //! \brief Returns a Mach-O image’s source version.
  //!
  //! This information comes from a Mach-O image’s `LC_SOURCE_VERSION` load
  //! command. For Mach-O images without this load command, `0` will be
  //! returned.
  uint64_t SourceVersion() const { return source_version_; }

  //! \brief Returns a Mach-O image’s UUID.
  //!
  //! This information comes from a Mach-O image’s `LC_UUID` load command. For
  //! Mach-O images without this load command, a zeroed-out UUID value will be
  //! returned.
  //
  // UUID is a name in this scope (referring to this method), so the parameter’s
  // type needs to be qualified with |crashpad::|.
  void UUID(crashpad::UUID* uuid) const;

  //! \brief Returns the dynamic linker’s pathname.
  //!
  //! The dynamic linker is normally /usr/lib/dyld.
  //!
  //! For executable images (those with file type `MH_EXECUTE`), this is the
  //! name provided in the `LC_LOAD_DYLINKER` load command, if any. For dynamic
  //! linker images (those with file type `MH_DYLINKER`), this is the name
  //! provided in the `LC_ID_DYLINKER` load command. In other cases, this will
  //! be empty.
  std::string DylinkerName() const { return dylinker_name_; }

 private:
  // A generic helper routine for the other Read*Command() methods.
  template <typename T>
  bool ReadLoadCommand(mach_vm_address_t load_command_address,
                       const std::string& load_command_info,
                       uint32_t expected_load_command_id,
                       T* load_command);

  // The Read*Command() methods are subroutines called by Initialize(). They are
  // responsible for reading a single load command. They may update the member
  // fields of their MachOImageReader object. If they can’t make sense of a load
  // command, they return false.
  bool ReadSegmentCommand(mach_vm_address_t load_command_address,
                          const std::string& load_command_info);
  bool ReadSymTabCommand(mach_vm_address_t load_command_address,
                         const std::string& load_command_info);
  bool ReadDySymTabCommand(mach_vm_address_t load_command_address,
                           const std::string& load_command_info);
  bool ReadIdDylibCommand(mach_vm_address_t load_command_address,
                          const std::string& load_command_info);
  bool ReadDylinkerCommand(mach_vm_address_t load_command_address,
                           const std::string& load_command_info);
  bool ReadUUIDCommand(mach_vm_address_t load_command_address,
                       const std::string& load_command_info);
  bool ReadSourceVersionCommand(mach_vm_address_t load_command_address,
                                const std::string& load_command_info);
  bool ReadUnexpectedCommand(mach_vm_address_t load_command_address,
                             const std::string& load_command_info);

  PointerVector<MachOImageSegmentReader> segments_;
  std::map<std::string, size_t> segment_map_;
  std::string module_info_;
  std::string dylinker_name_;
  crashpad::UUID uuid_;
  mach_vm_address_t address_;
  mach_vm_size_t size_;
  mach_vm_size_t slide_;
  uint64_t source_version_;
  scoped_ptr<process_types::symtab_command> symtab_command_;
  scoped_ptr<process_types::dysymtab_command> dysymtab_command_;
  scoped_ptr<process_types::dylib_command> id_dylib_command_;
  ProcessReader* process_reader_;  // weak
  uint32_t file_type_;
  InitializationStateDcheck initialized_;

  DISALLOW_COPY_AND_ASSIGN(MachOImageReader);
};

}  // namespace crashpad

#endif  // CRASHPAD_UTIL_MAC_MACH_O_IMAGE_READER_H_