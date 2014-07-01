// Copyright 2013 Google Inc.
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

#include "tap_parser.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <unistd.h>

#include "error.h"
#include "text.h"


/// Name of a regex error type.
static const char* const regex_error_type = "regex";

/// Representation of a regex error.
struct regex_error_data {
    /// Value of the error code captured during the error creation.
    int original_code;

    /// Value of the regex captured during the error creation.
    regex_t original_preg;

    /// Explanation of the problem that lead to the error.
    char description[4096];
};
/// Shorthand for a regex_error_data structure.
typedef struct regex_error_data regex_error_data_t;


/// Generates a user-friendly representation of the error.
///
/// \pre The error must be set.
///
/// \param error Error for which to generate a message.
/// \param output_buffer Buffer to hold the generated message.
/// \param output_size Length of output_buffer.
///
/// \return The number of bytes written to output_buffer, or a negative value if
/// there was an error.
static int
regex_format(const kyua_error_t error, char* const output_buffer,
            const size_t output_size)
{
    assert(kyua_error_is_type(error, regex_error_type));

    const regex_error_data_t* data = kyua_error_data(error);
    int prefix_length = snprintf(output_buffer, output_size, "%s: ",
                                 data->description);
    (void)regerror(data->original_code, &data->original_preg,
                   output_buffer + prefix_length, output_size - prefix_length);
    return strlen(output_buffer);
}


/// Releases the contents of the error.
///
/// \param opaque_data Internal data of a regex error.
static void
regex_free(void* opaque_data)
{
    regex_error_data_t* data = opaque_data;
    regfree(&data->original_preg);
}


/// Constructs a new regex error.
///
/// \param original_code regex error code for this error.
/// \param original_preg Original regex for this error.  Takes ownership.
/// \param description Textual description of the problem.
/// \param ... Positional arguments for the description.
///
/// \return The generated error.
static kyua_error_t
regex_error_new(const int original_code, regex_t* original_preg,
                const char* description, ...)
{
    va_list ap;

    const size_t data_size = sizeof(regex_error_data_t);
    regex_error_data_t* data = (regex_error_data_t*)malloc(data_size);
    if (data == NULL) {
        regfree(original_preg);
        return kyua_oom_error_new();
    }

    data->original_code = original_code;
    data->original_preg = *original_preg;
    va_start(ap, description);
    (void)vsnprintf(data->description, sizeof(data->description),
                    description, ap);
    va_end(ap);

    return kyua_error_new(regex_error_type, data, data_size, regex_format,
                          regex_free);
}


/// Extracts a regex match as a long.
///
/// \param line Input line in which the match lives.
/// \param match Match as returned by regexec.
/// \param [out] output Pointer to the parsed long value.
///
/// \return NULL if all is OK; a pointer to a constant string to an error
/// message otherwise.
static const char*
regex_match_too_long(const char* line, const regmatch_t* match, long* output)
{
    char buffer[64];
    const size_t length = match->rm_eo - match->rm_so;
    if (length > sizeof(buffer) - 1)
        return "Plan line too long";
    memcpy(buffer, line + match->rm_so, length);
    buffer[length] = '\0';

    errno = 0;
    char *endptr;
    const long tmp = strtol(buffer, &endptr, 10);
    assert(buffer[0] != '\0' && *endptr == '\0');  // Input is a number.
    if (errno == ERANGE || (tmp == LONG_MAX || tmp == LONG_MIN))
        return "Plan line includes out of range numbers";
    else {
        *output = tmp;
        return NULL;
    }
}


/// Attempts to parse a TAP plan line.
///
/// \param line The line to parse from the output of the TAP test program.
/// \param [in,out] summary Summary of the current status of the parsing.
///     Updated if a new plan is encountered.
///
/// \return An error object if there are problems parsing the input line or if
/// a duplicate plan has been encountered (as described by summary).
/// OK otherwise.
kyua_error_t
kyua_tap_try_parse_plan(const char* line, kyua_tap_summary_t* summary)
{
    regmatch_t matches[4];
    kyua_error_t kyua_error;
    regex_t preg;
    const char* error;
    long first_index, last_index;
    int code;

    kyua_error = kyua_error_ok();

    code = regcomp(&preg, "^([0-9]+)\\.\\.([0-9]+)(.*#.*)?$",
        REG_EXTENDED);
    if (code != 0) {
        kyua_error = regex_error_new(code, &preg, "regcomp failed");
        goto end;
    }

    code = regexec(&preg, line, sizeof(matches)/sizeof(*matches), matches, 0);
    if (code != 0) {
        if (code != REG_NOMATCH)
            kyua_error = regex_error_new(code, &preg, "regexec failed");
        goto end;
    }
    regfree(&preg);

    if (summary->first_index != -1) {
        summary->parse_error = "Output includes two test plans";
        goto end;
    }

    error = regex_match_too_long(line, &matches[1], &first_index);
    if (error != NULL) {
        summary->parse_error = error;
        goto end;
    }

    error = regex_match_too_long(line, &matches[2], &last_index);
    if (error != NULL) {
        summary->parse_error = error;
        goto end;
    }

    summary->skipped_all = false;
    if (matches[3].rm_so != -1 && matches[3].rm_eo != -1) {
        if (matches[3].rm_eo - matches[3].rm_so > 1024) {
            summary->parse_error = "Description attached to plan too long";
            goto end;
        }

        if (strstr(line + matches[3].rm_so, "SKIP") != NULL) {
            if (summary->ok_count || summary->not_ok_count ||
                summary->bail_out) {
                summary->parse_error= "No plan found in TAP output";
            }
            summary->skipped_all = true;
        }
    }

    if (!summary->skipped_all && last_index < first_index) {
        summary->parse_error = "Test plan is reversed";
    } else {
        summary->first_index = first_index;
        summary->last_index = last_index;
    }

end:
    regfree(&preg);
    return kyua_error;
}


/// Parses the output of a TAP test program.
///
/// \param fd Descriptor from which to read the output.  Grabs ownership.
/// \param output Stream to which to print the output as it is read.
/// \param [out] summary Filled in with the details of the parsing.  Only valid
///     if the function returns OK.
///
/// \return OK if the parsing succeeds (regardless of whether the test program
/// exits successfully or not); an error otherwise.
kyua_error_t
kyua_tap_parse(const int fd, FILE* output, kyua_tap_summary_t* summary)
{
    regmatch_t simple_test_matches[3], test_matches[5];
    kyua_error_t error;
    regex_t simple_test_reg;
    regex_t test_reg;
    char line[1024];  // It's ugly to have a limit, but it's easier this way.
    int code;

    FILE* input = fdopen(fd, "r");
    if (input == NULL) {
        close(fd);
        return kyua_libc_error_new(errno, "fdopen(3) failed");
    }

    kyua_tap_summary_new(*summary);

    /// There are two flavors of grammar with TAP:
    /// 1. "simple tests" -> `($ok)\s+([0-9]+)\s*(:\s*([^#]+))?`?
    /// 2. "tests" ->        `($ok)\s*([0-9]+)?\s*(.*)`
    code = regcomp(&simple_test_reg, "^(not )?ok[ \t]+[0-9]+[ \t]+([^#]+)?$",
        REG_EXTENDED);
    if (code != 0) {
        error = regex_error_new(code, &simple_test_reg, "regcomp failed");
        goto end2;
    }

    code = regcomp(&test_reg, "^(not )?ok[ \t]*([0-9]+)?([^#]+)?(.*)?$",
        REG_EXTENDED);

    if (code != 0) {
        regfree(&simple_test_reg);
        error = regex_error_new(code, &test_reg, "regcomp failed");
        goto end;
    }

    error = kyua_error_ok();

    while (!kyua_error_is_set(error) &&
           summary->parse_error == NULL && !summary->bail_out &&
           kyua_text_fgets_no_newline(line, sizeof(line), input) != NULL &&
           strcmp(line, "") != 0) {

        fprintf(output, "%s\n", line);

        error = kyua_tap_try_parse_plan(line, summary);
        if (kyua_error_is_set(error)) {
            continue;
        }

        if (strstr(line, "Bail out!") == line) {
            summary->bail_out = true;
            continue;
        }

        code = regexec(&simple_test_reg, line,
            sizeof(simple_test_matches)/sizeof(*simple_test_matches),
            simple_test_matches, 0);

        if (code == 0) {
            if (simple_test_matches[1].rm_so != -1 &&
                simple_test_matches[1].rm_eo != -1) {
                summary->not_ok_count++;
            } else {
                summary->ok_count++;
            }
        } else {
            if (code != REG_NOMATCH) {
                error = regex_error_new(code, &simple_test_reg, "regexec failed");
            }
        }
        // There was a match per the simple_test pattern
        if (code != REG_NOMATCH) {
            continue;
        }

        code = regexec(&test_reg, line, sizeof(test_matches)/sizeof(*test_matches),
            test_matches, 0);
        if (code == 0) {
            /// not SKIP/TODO and "not ok"
            //
            /// NOTE: the TAP protocol states that all SKIP/TODO testcases
            /// should be marked "ok".
            ///
            /// XXX: TODO should be xfail in kyua-speak.
            if (test_matches[1].rm_so != -1 && test_matches[1].rm_eo != -1 &&
                test_matches[4].rm_so != -1 && test_matches[4].rm_eo != -1 &&
                strcasestr(line + test_matches[4].rm_so, "SKIP") == NULL &&
                strcasestr(line + test_matches[4].rm_so, "TODO") == NULL) {
                summary->not_ok_count++;
            } else {
                summary->ok_count++;
            }
        } else {
            if (code != REG_NOMATCH) {
                error = regex_error_new(code, &test_reg, "regexec failed");
            }
        }

    }

    if (summary->first_index == -1) {
        summary->parse_error = "No plan found in TAP output";
    } else if (summary->parse_error == NULL && !summary->bail_out) {
        const long exp_count = summary->last_index - summary->first_index + 1;
        const long actual_count = summary->ok_count + summary->not_ok_count;
        if (exp_count != actual_count) {
            summary->parse_error = "Reported plan differs from actual executed "
                "tests";
        }
    }

end:
    regfree(&simple_test_reg);
end2:
    regfree(&test_reg);

    fclose(input);

    return error;
}
