/***************************************************************
 *
 * Copyright (C) 1990-2010, Condor Team, Computer Sciences Department,
 * University of Wisconsin-Madison, WI.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You may
 * obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************/

#ifndef _stl_string_utils_h_
#define _stl_string_utils_h_ 1

#include <string>
#include <string.h>
#include <vector>
#include <climits>
#include "condor_header_features.h"

// formatstr() will try to write to a fixed buffer first, for reasons of 
// efficiency.  This is the size of that buffer.
#define STL_STRING_UTILS_FIXBUF 500

// Analogous to standard sprintf(), but writes to std::string 's', and is
// memory/buffer safe.
int formatstr(std::string& s, const char* format, ...) CHECK_PRINTF_FORMAT(2,3);
int vformatstr(std::string& s, const char* format, va_list pargs);
int vformatstr_impl(std::string& s, bool concat, const char* format, va_list pargs);

// Returns number of replacements actually performed, or -1 if from is empty.
int replace_str( std::string & str, const std::string & from, const std::string & to, size_t start = 0 );

// Appending versions of above.
// These return number of new chars appended.
int formatstr_cat(std::string& s, const char* format, ...) CHECK_PRINTF_FORMAT(2,3);
int vformatstr_cat(std::string& s, const char* format, va_list pargs);

// to replace MyString with std::string we need a compatible read-line function
bool readLine(std::string& dst, FILE *fp, bool append = false);

//Return true iff the given string is a blank line.
int blankline ( const char *str );

// fast case-insensitive search for attr in a list of attributes
// returns a pointer to the first character in the list after the matching attribute
// returns NULL if no match.  DO NOT USE ON ARBITRARY strings. this can generate
// false matches if the strings contain characters other than those valid for classad attributes
// attributes in the list should be separated by comma, space or newline
const char * is_attr_in_attr_list(const char * attr, const char * list);

bool chomp(std::string &str);
void trim(std::string &str);
void trim_quotes(std::string &str, std::string quotes);
void lower_case(std::string &str);
void upper_case(std::string &str);
void title_case(std::string &str); // capitalize each word

const char * empty_if_null(const char * c_str);

// Return a string based on string src, but for each character in Q that
// occurs in src, insert the character escape before it.
// For example, for src="Alain", Q="abc", and escape='_', the result will
// be "Al_ain".
std::string EscapeChars(const std::string& src, const std::string& Q, char escape);

// Return a string based on string src, but remove all ANSI terminal escape
// sequences.  Useful when taking stderr output to remove ANSI color codes.
std::string RemoveANSIcodes(const std::string& src);

// returns true if pre is non-empty and str is the same as pre up to pre.size()
bool starts_with(const std::string& str, const std::string& pre);
bool starts_with_ignore_case(const std::string& str, const std::string& pre);

bool ends_with(const std::string& str, const std::string& post);

// case insensitive sort functions for use with std::sort
bool sort_ascending_ignore_case(std::string const & a, std::string const & b);
bool sort_decending_ignore_case(std::string const & a, std::string const & b);

std::vector<std::string> split(const std::string& str, const char* delim=", \t\r\n", bool trim=true);
std::string join(const std::vector<std::string> &list, const char* delim);

bool contains(const std::vector<std::string> &list, const std::string& str);
bool contains(const std::vector<std::string> &list, const char* str);
bool contains_anycase(const std::vector<std::string> &list, const std::string& str);
bool contains_anycase(const std::vector<std::string> &list, const char* str);

bool contains_prefix(const std::vector<std::string> &list, const std::string& str);
bool contains_prefix(const std::vector<std::string> &list, const char* str);
bool contains_prefix_anycase(const std::vector<std::string> &list, const std::string& str);
bool contains_prefix_anycase(const std::vector<std::string> &list, const char* str);

bool contains_withwildcard(const std::vector<std::string> &list, const std::string& str);
bool contains_withwildcard(const std::vector<std::string> &list, const char* str);
bool contains_anycase_withwildcard(const std::vector<std::string> &list, const std::string& str);
bool contains_anycase_withwildcard(const std::vector<std::string> &list, const char* str);

bool contains_prefix_withwildcard(const std::vector<std::string> &list, const std::string& str);
bool contains_prefix_withwildcard(const std::vector<std::string> &list, const char* str);
bool contains_prefix_anycase_withwildcard(const std::vector<std::string> &list, const std::string& str);
bool contains_prefix_anycase_withwildcard(const std::vector<std::string> &list, const char* str);

// scan an input string for path separators, returning a pointer into the input string that is
// the first charactter after the last input separator. (i.e. the filename part). if the input
// string contains no path separater, the return is the same as the input, if the input string
// ends with a path separater, the return is a pointer to the null terminator.
const char * filename_from_path(const char * pathname);
inline char * filename_from_path(char * pathname) { return const_cast<char*>(filename_from_path(const_cast<const char *>(pathname))); }
size_t filename_offset_from_path(std::string & pathname);

/** Clears the string and fills it with a
 *	randomly generated set derived from 'set' of len characters. */
void randomlyGenerateInsecure(std::string &str, const char *set, int len);
//void randomlyGeneratePRNG(std::string &str, const char *set, int len);

/** Clears the string and fills it with
 *	randomly generated [0-9a-f] values up to len size */
void randomlyGenerateInsecureHex(std::string &str, int len);

/** Clears the string and fills it with
 *	randomly generated alphanumerics and punctuation up to len size */
void randomlyGenerateShortLivedPassword(std::string &str, int len);

// iterate a Null terminated string constant in the same way that StringList does in initializeFromString
// Use this class instead of creating a throw-away StringList just so you can iterate the tokens in a string.
//
// NOTE: there are some subtle differences between this code and StringList::initializeFromString.
// StringList ALWAYS tokenizes on whitespace regardlist of what delims is set to, but
// this iterator does not, if you want to tokenize on whitespace, then delims must contain the whitepace characters.
//
// NOTE also, this class does NOT copy the string that it is passed.  You must insure that it remains in scope and is
// unchanged during iteration.  This is trivial for string literals, of course.
class StringTokenIterator {
public:
	StringTokenIterator(const char *s = NULL, int res=40, const char *delim = ", \t\r\n" ) : str(s), delims(delim), ixNext(0), pastEnd(false) { current.reserve(res); };
	StringTokenIterator(const std::string & s, int res=40, const char *delim = ", \t\r\n" ) : str(s.c_str()), delims(delim), ixNext(0), pastEnd(false) { current.reserve(res); };

	void rewind() { ixNext = 0; pastEnd = false;}
	const char * next() { const std::string * s = next_string(); return s ? s->c_str() : NULL; }
	const char * first() { ixNext = 0; pastEnd = false; return next(); }
	const char * remain() { if (!str || !str[ixNext]) return NULL; return str + ixNext; }

	int next_token(int & length); // return start and length of next token or -1 if no tokens remain
	const std::string * next_string(); // return NULL or a pointer to current token

	// Allow us to use this as a bona-fide STL iterator
	using iterator_category = std::input_iterator_tag;
	using value_type        = std::string;
	using difference_type   = std::ptrdiff_t;
	using pointer           = std::string *;
	using reference         = std::string *;

	StringTokenIterator begin() const {
		StringTokenIterator sti{str, 0, delims};
		sti.next();
		return sti;
	}

	StringTokenIterator end() const {
		StringTokenIterator sti{str, 0, delims};
		sti.ixNext = strlen(str);
		sti.pastEnd = true;
		return sti;
	}

	std::string &operator*() {
		return current;
	}
	
	StringTokenIterator &operator++() {
		next();
		return *this;
	}

friend bool operator==(const StringTokenIterator &lhs, const StringTokenIterator &rhs) {
	return lhs.ixNext == rhs.ixNext && lhs.pastEnd == rhs.pastEnd;
}

friend bool operator!=(const StringTokenIterator &lhs, const StringTokenIterator &rhs) {
	return (lhs.ixNext != rhs.ixNext) || (lhs.pastEnd != rhs.pastEnd);
}

protected:
	const char * str;   // The string we are tokenizing. it's not a copy, caller must make sure it continues to exist.
	const char * delims;
	std::string current;
	size_t ixNext;
	bool pastEnd;
};

// Case insensitive string_view
// Mostly cribbed from cppreference.com/w/cpp/string/char_traits
struct case_char_traits : public std::char_traits<char>
{
    static constexpr char to_upper(char ch)
    {
		if ((ch >= 'a') && (ch <= 'z')) {
			return ch - 'a' + 'A';
		} else {
			return ch;
		}
    }
 
    static constexpr bool eq(char c1, char c2)
    {
        return to_upper(c1) == to_upper(c2);
    }
 
    static constexpr bool lt(char c1, char c2)
    {
         return to_upper(c1) < to_upper(c2);
    }
 
    static constexpr int compare(const char* s1, const char* s2, std::size_t n)
    {
        while (n-- != 0)
        {
            if (to_upper(*s1) < to_upper(*s2))
                return -1;
            if (to_upper(*s1) > to_upper(*s2))
                return 1;
            ++s1;
            ++s2;
        }
        return 0;
    }
 
    static constexpr const char* find(const char* s, std::size_t n, char a)
    {
        auto const ua (to_upper(a));
        while (n-- != 0) 
        {
            if (to_upper(*s) == ua)
                return s;
            s++;
        }
        return nullptr;
    }
};

using istring_view = std::basic_string_view<char, case_char_traits>;

#endif // _stl_string_utils_h_
