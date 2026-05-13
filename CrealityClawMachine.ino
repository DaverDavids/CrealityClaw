// Returns true if the string is a valid number (integer or float, optional leading minus/plus)
bool isValidNumber(String s) {
  s.trim();
  if (s.length() == 0) return false;
  int start = 0;
  if (s.charAt(0) == '-' || s.charAt(0) == '+') {
    start = 1;
    if (s.length() == 1) return false;
  }
  bool hasDot = false;
  for (int i = start; i < (int)s.length(); i++) {
    char c = s.charAt(i);
    if (c == '.') {
      if (hasDot) return false;
      hasDot = true;
    } else if (!isDigit(c)) {
      return false;
    }
  }
  return true;
}

bool extractArg(String message, String pattern, String& varName, String& varValue) {
  if (pattern.length() == 0) return false;

  int openBrace = pattern.indexOf('{');
  int closeBrace = pattern.indexOf('}', openBrace);

  // If no variable placeholder, just check if the text exists
  if (openBrace < 0 || closeBrace < 0) {
    return message.indexOf(pattern) >= 0;
  }

  String prefix = pattern.substring(0, openBrace);
  String name = pattern.substring(openBrace + 1, closeBrace);

  // Find the prefix at a token boundary (start of message, or preceded by space/comma)
  int searchFrom = 0;
  int startIdx = -1;
  while (true) {
    int found = message.indexOf(prefix, searchFrom);
    if (found < 0) break;
    bool atToken = (found == 0 ||
                    message.charAt(found - 1) == ' ' ||
                    message.charAt(found - 1) == ',');
    if (atToken) { startIdx = found; break; }
    searchFrom = found + 1;
  }
  if (startIdx < 0) return false;

  // Value starts after the prefix; allow one optional space (e.g. "x 50")
  int valStart = startIdx + prefix.length();
  if (valStart < (int)message.length() && message.charAt(valStart) == ' ') {
    valStart++;
  }

  // Stop at next space, comma, or end-of-string
  int spaceIdx = message.indexOf(' ', valStart);
  int commaIdx = message.indexOf(',', valStart);
  int valEnd = message.length();
  if (spaceIdx >= 0 && spaceIdx < valEnd) valEnd = spaceIdx;
  if (commaIdx >= 0 && commaIdx < valEnd) valEnd = commaIdx;

  String val = message.substring(valStart, valEnd);
  val.trim();

  if (val.length() == 0) return false;

  // For coordinate variables (single uppercase letter: X, Y, Z, etc.),
  // reject non-numeric values instead of silently passing garbage to toFloat()
  bool isCoordVar = (name.length() == 1 &&
                     name.charAt(0) >= 'A' && name.charAt(0) <= 'Z');
  if (isCoordVar && !isValidNumber(val)) return false;

  varName = name;
  varValue = val;
  return true;
}