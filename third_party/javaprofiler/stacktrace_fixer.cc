// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>

#include <algorithm>

#include "third_party/javaprofiler/stacktrace_fixer.h"

namespace google {
namespace javaprofiler {

namespace {

// Simplifies a given string searching an occurrence of the trigger string
// followed by specified suffix chars and removing those. For example,
// calling the function with ("foo123bar", "foo", "321") returns "foobar".
template <size_t M, size_t N>
void SimplifySuffixedName(std::string *name, const char (&trigger)[M],
                          const char (&suffix_chars)[N]) {
  size_t first = 0;
  while ((first = name->find(trigger, first)) != std::string::npos) {
    first += M - 1;  // Exclude last zero char.
    size_t last = name->find_first_not_of(suffix_chars, first, N);
    if (last == std::string::npos) {
      name->erase(first);
      break;
    }
    name->erase(first, last - first);
  }
}

// Simplifies the name of a method in a dynamic class (with '$$FastClassBy*$$'
// or '$$EnhancedBy*$$' in its name) to make it more human readable, and group
// related functions under a single name. This could be done with a regexp
// replacement, but including RE2 increases the size of the agent.
void SimplifyDynamicClassName(std::string *name) {
  // Replace $$[0-9a-f]+ by $$ to remove unique values, for example in
  // $FastClassByCGLIB$$fd6bdf6d.invoke.
  SimplifySuffixedName(name, "$$", "0123456789abcdef");
}

// Simplifies the name of a lambda method to replace $$Lambda$[0-9]+\.[0-9]+ by
// $$Lambda$ to remove unique values, for example in
// com.google.something.Something$$Lambda$197.1849072452.run.
void SimplifyLambdaName(std::string *name) {
  constexpr char trigger[] = "$$Lambda$";
  constexpr char digits[] = "0123456789";
  const size_t trigger_length = strlen(trigger);

  // Assume and handle just one instance of a $$Lambda$ pattern.
  size_t first = name->find(trigger);
  if (first == std::string::npos) {
    return;
  }
  first += trigger_length;
  if (first >= name->size() || !isdigit((*name)[first])) {
    return;
  }
  size_t last = name->find_first_not_of(digits, first);
  if (last == std::string::npos || (*name)[last] != '.') {
    return;
  }
  last++;  // skip the dot
  if (last >= name->size() || !isdigit((*name)[last])) {
    return;
  }
  last = name->find_first_not_of(digits, last);
  if (last == std::string::npos) {
    name->erase(first);
    return;
  }
  name->erase(first, last - first);
}

constexpr char digits[] = "0123456789";

// Simplifies the name of a method generated by the runtime as a reflection
// stub. See the test file for examples, or generateName() in
// jdk/internal/reflect/MethodAccessorGenerator.java.
void SimplifyInternalReflectionMethodName(std::string *name) {
  SimplifySuffixedName(
      name,
      "jdk.internal.reflect.GeneratedConstructorAccessor",
      digits);

  SimplifySuffixedName(
      name,
      "jdk.internal.reflect.GeneratedMethodAccessor",
      digits);

  SimplifySuffixedName(
      name,
      "jdk.internal.reflect.GeneratedSerializationConstructorAccessor",
      digits);
}

// Simplifies the name of a method generated by the runtime as a reflection
// stub. See the test file for examples, or generateName() in
// sun/reflect/MethodAccessorGenerator.java.
void SimplifyReflectionMethodName(std::string *name) {
  SimplifySuffixedName(
      name,
      "sun.reflect.GeneratedConstructorAccessor",
      digits);

  SimplifySuffixedName(
      name,
      "sun.reflect.GeneratedMethodAccessor",
      digits);

  SimplifySuffixedName(
      name,
      "sun.reflect.GeneratedSerializationConstructorAccessor",
      digits);
}

std::string ParseMethodTypeSignatureWithReturn(const char *buffer,
                                               int buffer_size, int *pos);

// JVM type signature parser and pretty printer.  It returns the pretty-printed
// string; it also modifies the position pointer in the input buffer.
// TODO: Move ParseFieldType to replace the string directly.
std::string ParseFieldType(const char *buffer, int buffer_size, int *pos) {
  if (*pos >= buffer_size) {
    return "<error: end of buffer reached>";
  }

  char type = buffer[*pos];
  (*pos)++;
  switch (type) {
    case 'B': {
      return "byte";
    }
    case 'C': {
      return "char";
    }
    case 'D': {
      return "double";
    }
    case 'F': {
      return "float";
    }
    case 'I': {
      return "int";
    }
    case 'J': {
      return "long";
    }
    case 'S': {
      return "short";
    }
    case 'Z': {
      return "boolean";
    }
    case 'V': {
      return "void";
    }
    case 'L': {
      // Parse the following string ending with semicolon.
      const char *begin = buffer + *pos;
      const char *end = buffer + buffer_size;
      for (const char *cur = begin; cur < end; cur++) {
        if (*cur == ';') {
          *pos += (cur - begin) + 1;
          return std::string(begin, cur);
        }
      }
      *pos = buffer_size;
      return "<error: end of string reached>";
    }
    case '[': {
      // Recursively parse the array type.
      std::string s = ParseFieldType(buffer, buffer_size, pos);
      return s + "[]";
    }
    case '(': {
      // -1 to start back at the '(', it is more coherent with the method call
      // to start with the '(' but goes against how the loop is created.
      (*pos)--;
      return ParseMethodTypeSignatureWithReturn(buffer, buffer_size, pos);
    }
    default: {
      return "<error: unknown type>";
    }
  }
}

inline bool AtSignatureEnd(const char* buffer, int buffer_size, int *pos) {
  return *pos >= buffer_size || buffer[*pos] == ')';
}

std::string ParseMethodTypeSignatureInternal(const char *buffer,
                                             int buffer_size, int *pos) {
  if (buffer == nullptr || pos == nullptr || *pos >= buffer_size ||
      buffer[*pos] != '(') {
    return "";
  }

  // Skip the '('.
  (*pos)++;

  std::string buf("(");
  while (!AtSignatureEnd(buffer, buffer_size, pos)) {
    buf.append(ParseFieldType(buffer, buffer_size, pos));
    if (!AtSignatureEnd(buffer, buffer_size, pos)) {
      buf.append(", ");
    }
  }

  if (*pos < buffer_size) {
    (*pos)++;
    buf.append(")");
  } else {
    buf.append(" <Method Signature Error: no ')'>");
  }
  return buf;
}

// JVM method type signature parser and pretty printer. It returns the
// pretty-printed string; it also modifies the position pointer in the input
// buffer.
std::string ParseMethodTypeSignature(const char *buffer, int buffer_size,
                                     int *pos) {
  return ParseMethodTypeSignatureInternal(buffer, buffer_size, pos);
}

std::string ParseMethodTypeSignatureWithReturn(const char *buffer,
                                               int buffer_size, int *pos) {
  std::string argument_string =
      ParseMethodTypeSignatureInternal(buffer, buffer_size, pos);
  if (argument_string.empty()) {
    return "";
  }

  // Something wrong happened if we don't finish with ')'.
  if (argument_string.back() != ')') {
    return argument_string;
  }

  std::string return_string = ParseFieldType(buffer, buffer_size, pos);
  return return_string + " " + argument_string;
}

}  // namespace

void SimplifyFunctionName(std::string *name) {
  SimplifyDynamicClassName(name);
  SimplifyLambdaName(name);
  SimplifyReflectionMethodName(name);
  SimplifyInternalReflectionMethodName(name);
}

void FixPath(std::string *s) { std::replace(s->begin(), s->end(), '/', '.'); }

void PrettyPrintSignature(std::string *s) {
  int pos = 0;
  std::string result = ParseFieldType(s->data(), s->length(), &pos);
  FixPath(&result);
  *s = result;
}

void FixMethodParameters(std::string *signature) {
  if (signature == nullptr || signature->empty() ||
      signature->at(0) != '(') {
    return;
  }

  // Not the fastest way of doing this: we could rework this to handle this in
  // one step.
  FixPath(signature);

  int len = signature->length();
  int pos = 0;
  *signature = ParseMethodTypeSignature(signature->c_str(), len, &pos);
}

}  // namespace javaprofiler
}  // namespace google
