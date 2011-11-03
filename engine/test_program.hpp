// Copyright 2010, 2011 Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors
//   may be used to endorse or promote products derived from this software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

/// \file engine/test_program.hpp
/// Interface to interact with test programs.
///
/// A test program is purely a collection of test cases.  The test program has
/// no identity by itself: it only exists to provide a consistent entry point
/// for all the test cases it contains and to group such test cases
/// semantically.  Therefore, this module provides no data type to represent the
/// test program.

#if !defined(ENGINE_TEST_PROGRAM_HPP)
#define ENGINE_TEST_PROGRAM_HPP

extern "C" {
#include <stdint.h>
}

#include <string>
#include <tr1/memory>
#include <vector>

#include "engine/test_case.hpp"
#include "utils/fs/path.hpp"

namespace engine {


/// Collection of test cases.
typedef std::vector< test_case_ptr > test_cases_vector;


/// Abstract representation of a test program.
///
/// Subclasses must use the same pimpl idiom using a shared_ptr to ensure that
/// copies are always shallow.  This is a prerequisite for unique_address() to
/// work properly.
class base_test_program {
    struct base_impl;
    std::tr1::shared_ptr< base_impl > _pbimpl;

    virtual test_cases_vector load_test_cases(void) const = 0;

public:
    base_test_program(const utils::fs::path&, const utils::fs::path&,
                      const std::string&);
    virtual ~base_test_program(void);

    intptr_t unique_address(void) const;

    const utils::fs::path& root(void) const;
    const utils::fs::path& relative_path(void) const;
    const utils::fs::path absolute_path(void) const;
    const std::string& test_suite_name(void) const;
    const test_cases_vector& test_cases(void) const;
};


/// Pointer to a test program.
typedef std::tr1::shared_ptr< base_test_program > test_program_ptr;


/// Collection of test programs.
typedef std::vector< test_program_ptr > test_programs_vector;


}  // namespace engine

#endif  // !defined(ENGINE_TEST_PROGRAM_HPP)
