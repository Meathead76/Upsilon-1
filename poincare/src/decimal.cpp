#include <poincare/decimal.h>
#include <poincare/rational.h>
#include <poincare/opposite.h>
#include <poincare/infinity.h>
#include <poincare/undefined.h>
#include <poincare/layout_helper.h>
#include <poincare/serialization_helper.h>
#include <poincare/ieee754.h>
#include <ion/unicode/utf8_decoder.h>
#include <ion/unicode/utf8_helper.h>
#include <assert.h>
#include <cmath>
#include <utility>
#include <algorithm>
#ifdef PLATFORM_ESP32
#include <Arduino.h>
#endif

namespace Poincare {

void removeZeroAtTheEnd(Integer * i, int minimalNumbersOfDigits = -1) {
  /* Remove the zeroes at the end of an integer, respecting the minimum number
   * of digits asked for.
   *
   * For instance :
   *
   * i = 1000
   * removeZeroAtTheEnd(&i, 2)
   * assert(i==10)
   *
   * i = 1000
   * removeZeroAtTheEnd(&i, -1)
   * assert(i==1)
   */

  if (i->isZero()) {
    return;
  }

  /* If we check the number of digits, we want *i to stay outside of the
   * interval ]-10^numberDigits; 10^numberDigits[. */
  const bool shouldCheckMinimalNumberOfDigits = minimalNumbersOfDigits > 0;
  Integer minimum = shouldCheckMinimalNumberOfDigits ?
    Integer((int64_t)std::pow(10.0, minimalNumbersOfDigits-1)) :
    Integer::Overflow(false);
  Integer minusMinimum = shouldCheckMinimalNumberOfDigits ?
    Integer(-(int64_t)std::pow(10.0, minimalNumbersOfDigits-1)) :
    Integer::Overflow(false);

  Integer base = Integer(10);
  IntegerDivision d = Integer::Division(*i, base);
  while (d.remainder.isZero()) {
    if (shouldCheckMinimalNumberOfDigits && (Integer::NaturalOrder(d.quotient, minimum) < 0 && Integer::NaturalOrder(d.quotient, minusMinimum) > 0)) {
      break;
    }
    *i = d.quotient;
    d = Integer::Division(*i, base);
  }
  assert(!i->isOverflow());
}

DecimalNode::DecimalNode(const native_uint_t * mantissaDigits, uint8_t mantissaSize, int exponent, bool negative) :
  m_negative(negative),
  m_exponent(exponent),
  m_numberOfDigitsInMantissa(mantissaSize)
{
  memcpy(m_mantissa, mantissaDigits, mantissaSize*sizeof(native_uint_t));
}

Integer DecimalNode::signedMantissa() const {
  return Integer::BuildInteger((native_uint_t *)m_mantissa, m_numberOfDigitsInMantissa, m_negative);
}

Integer DecimalNode::unsignedMantissa() const {
  return Integer::BuildInteger((native_uint_t *)m_mantissa, m_numberOfDigitsInMantissa, false);
}

static size_t DecimalSize(uint8_t numberOfDigitsInMantissa) {
  return sizeof(DecimalNode)+ sizeof(native_uint_t)*numberOfDigitsInMantissa;
}

size_t DecimalNode::size() const {
  return DecimalSize(m_numberOfDigitsInMantissa);
}

Expression DecimalNode::setSign(Sign s, ReductionContext reductionContext) {
  assert(s == ExpressionNode::Sign::Positive || s == ExpressionNode::Sign::Negative);
  return Decimal(this).setSign(s);
}

int DecimalNode::simplificationOrderSameType(const ExpressionNode * e, bool ascending, bool canBeInterrupted, bool ignoreParentheses) const {
  if (!ascending) {
    return e->simplificationOrderSameType(this, true, canBeInterrupted, ignoreParentheses);
  }
  assert(e->type() == Type::Decimal);
  const DecimalNode * other = static_cast<const DecimalNode *>(e);
  if (m_negative && !other->m_negative) {
    return -1;
  }
  if (!m_negative && other->m_negative) {
    return 1;
  }
  assert(m_negative == other->m_negative);
  int unsignedComparison = 0;
  if (m_exponent < other->m_exponent) {
    unsignedComparison = -1;
  } else if (m_exponent > other->m_exponent) {
    unsignedComparison = 1;
  } else {
    assert(m_exponent == other->m_exponent);
    assert(exponent() == other->exponent());
    unsignedComparison = Integer::NaturalOrder(unsignedMantissa(), other->unsignedMantissa());
  }
  return ((int)Number(this).sign())*unsignedComparison;
}

Expression DecimalNode::shallowReduce(ReductionContext reductionContext) {
  return Decimal(this).shallowReduce();
}

Expression DecimalNode::shallowBeautify(ReductionContext * reductionContext) {
  return Decimal(this).shallowBeautify();
}

Layout DecimalNode::createLayout(Preferences::PrintFloatMode floatDisplayMode, int numberOfSignificantDigits) const {
  char buffer[k_maxBufferSize];
  int numberOfChars = convertToText(buffer, k_maxBufferSize, floatDisplayMode, numberOfSignificantDigits);
  return LayoutHelper::String(buffer, numberOfChars);
}

int DecimalNode::serialize(char * buffer, int bufferSize, Preferences::PrintFloatMode floatDisplayMode, int numberOfSignificantDigits) const {
  return convertToText(buffer, bufferSize, floatDisplayMode, numberOfSignificantDigits);
}

int DecimalNode::convertToText(char * buffer, int bufferSize, Preferences::PrintFloatMode mode, int numberOfSignificantDigits) const {
#ifdef PLATFORM_ESP32
  Serial.printf("[DEC] convertToText bufSize=%d mode=%d nSigDig=%d neg=%d exp=%d nDigits=%d\n",
                bufferSize, (int)mode, numberOfSignificantDigits, m_negative, m_exponent, m_numberOfDigitsInMantissa);
  Serial.flush();
#endif
  if (bufferSize == 0) {
    return -1;
  }
  if (bufferSize == 1) {
    buffer[0] = 0;
    return 0;
  }
  if (unsignedMantissa().isZero()) {
    return SerializationHelper::CodePoint(buffer, bufferSize, '0'); // This already writes the null terminating char
  }

  // Compute the exponent
  int exponent = m_exponent;

  // Round the integer if m_mantissa > 10^numberOfSignificantDigits-1
  char tempBuffer[PrintFloat::k_numberOfStoredSignificantDigits+1];
#ifdef PLATFORM_ESP32
  Serial.printf("[DEC] getting unsignedMantissa...\n");
  Serial.flush();
#endif
  // Dump raw mantissa data before constructing Integer
#ifdef PLATFORM_ESP32
  Serial.printf("[DEC] raw mantissa words:");
  for (int _i = 0; _i < m_numberOfDigitsInMantissa; _i++) {
    Serial.printf(" [%d]=0x%08x", _i, m_mantissa[_i]);
  }
  Serial.printf("\n");
  Serial.flush();
#endif
  Integer m = unsignedMantissa();
#ifdef PLATFORM_ESP32
  Serial.printf("[DEC] got mantissa nDigits=%d, computing NumberOfBase10Digits...\n", m.numberOfDigits());
  Serial.flush();
#endif
  int numberOfDigitsInMantissa = Integer::NumberOfBase10DigitsWithoutSign(m);
#ifdef PLATFORM_ESP32
  Serial.printf("[DEC] numberOfDigitsInMantissa=%d\n", numberOfDigitsInMantissa);
  Serial.flush();
#endif
  if (numberOfDigitsInMantissa > numberOfSignificantDigits) {
#ifdef PLATFORM_ESP32
    Serial.printf("[DEC] rounding: nDigits=%d > nSigDig=%d\n", numberOfDigitsInMantissa, numberOfSignificantDigits);
    Serial.flush();
#endif
#ifdef PLATFORM_ESP32
    IntegerDivision d = Integer::Division(m, Integer((native_int_t)std::pow(10.0, numberOfDigitsInMantissa - numberOfSignificantDigits)));
    m = d.quotient;
    if (Integer::NaturalOrder(d.remainder, Integer((native_int_t)(5.0*std::pow(10.0, numberOfDigitsInMantissa-numberOfSignificantDigits-1)))) >= 0) {
#else
    IntegerDivision d = Integer::Division(m, Integer((int64_t)std::pow(10.0, numberOfDigitsInMantissa - numberOfSignificantDigits)));
    m = d.quotient;
    if (Integer::NaturalOrder(d.remainder, Integer((int64_t)(5.0*std::pow(10.0, numberOfDigitsInMantissa-numberOfSignificantDigits-1)))) >= 0) {
#endif
      m = Integer::Addition(m, Integer(1));
      // if 9999 was rounded to 10000, we need to update exponent and mantissa
      if (Integer::NumberOfBase10DigitsWithoutSign(m) > numberOfSignificantDigits) {
        exponent++;
        m = Integer::Division(m, Integer(10)).quotient;
      }
    }
#ifdef PLATFORM_ESP32
    Serial.printf("[DEC] rounding done\n");
    Serial.flush();
#endif
  }
#ifdef PLATFORM_ESP32
  Serial.printf("[DEC] before removeZeroes\n");
  Serial.flush();
#endif
  int exponentForEngineeringNotation = 0;
  int minimalNumberOfMantissaDigits = -1;
  bool removeZeroes = true;
  if (mode == Preferences::PrintFloatMode::Engineering) {
    exponentForEngineeringNotation = PrintFloat::EngineeringExponentFromBase10Exponent(exponent);
    minimalNumberOfMantissaDigits = PrintFloat::EngineeringMinimalNumberOfDigits(exponent, exponentForEngineeringNotation);
    int numberOfZeroesToAddForEngineering = PrintFloat::EngineeringNumberOfZeroesToAdd(minimalNumberOfMantissaDigits, Integer::NumberOfBase10DigitsWithoutSign(m));
    if (numberOfZeroesToAddForEngineering > 0) {
      for (int i = 0; i < numberOfZeroesToAddForEngineering; i ++) {
        m = Integer::Multiplication(m, Integer(10));
      }
      removeZeroes = false;
    }
  }

  /* Remove the final zeroes, that already existed or were created due to
   * rounding. For example 1.999 with 3 significant digits: the mantissa 1999 is
   * rounded to 2000. To avoid printing 2.000, we removeZeroAtTheEnd here. */
  if (removeZeroes) {
#ifdef PLATFORM_ESP32
    Serial.printf("[DEC] removeZeroAtTheEnd...\n");
    Serial.flush();
#endif
    removeZeroAtTheEnd(&m, minimalNumberOfMantissaDigits);
#ifdef PLATFORM_ESP32
    Serial.printf("[DEC] removeZeroAtTheEnd done\n");
    Serial.flush();
#endif
  }

  // Print the sign
  int currentChar = 0;
  if (m_negative) {
    assert(UTF8Decoder::CharSizeOfCodePoint('-') == 1);
    buffer[0] = '-';
    buffer[1] = 0;
    currentChar++;
    if (currentChar >= bufferSize-1) { return bufferSize-1; }
  }

#ifdef PLATFORM_ESP32
  Serial.printf("[DEC] serializing mantissa Integer...\n");
  Serial.flush();
#endif
  // Serialize the mantissa
  int mantissaLength = m.serialize(tempBuffer, PrintFloat::k_numberOfStoredSignificantDigits+1);
#ifdef PLATFORM_ESP32
  Serial.printf("[DEC] mantissa serialized: len=%d str='%.30s'\n", mantissaLength, tempBuffer);
  Serial.flush();
#endif

  // Assert that m is not +/-inf
  assert(strcmp(tempBuffer, Infinity::Name()) != 0);
  assert(!(UTF8Helper::CodePointIs(tempBuffer, '-') && strcmp(&tempBuffer[1], Infinity::Name()) == 0));

  // Stop here if m is undef
  if (strcmp(tempBuffer, Undefined::Name()) == 0) {
    currentChar += strlcpy(buffer+currentChar, tempBuffer, bufferSize-currentChar);
    return std::min(currentChar, bufferSize-1);
  }

  /* We force scientific mode if the number of digits before the dot is superior
   * to the number of significant digits (ie with 4 significant digits,
   * 12345 -> 1.235E4 or 12340 -> 1.234E4). */
  bool forceScientificMode = mode != Preferences::PrintFloatMode::Engineering && (mode == Preferences::PrintFloatMode::Scientific || exponent >= numberOfSignificantDigits);
  int numberOfRequiredDigits = mantissaLength;
  if (mode == Preferences::PrintFloatMode::Decimal && !forceScientificMode) {
    if (exponent < 0) {
      numberOfRequiredDigits = mantissaLength-exponent;
    } else {
      numberOfRequiredDigits = std::max(mantissaLength, exponent);
    }
  }

  /* Case 1: Engineering and Scientific mode. Three cases:
   * - the user chooses the scientific mode
   * - the exponent is too big compared to the number of significant digits, so
   *   we force the scientific mode to avoid inventing digits
   * - the number would be too long if we print it as a natural decimal */
  if (mode == Preferences::PrintFloatMode::Engineering || numberOfRequiredDigits > PrintFloat::k_numberOfStoredSignificantDigits || forceScientificMode) {
    if (mantissaLength == 1) {
      currentChar += strlcpy(buffer+currentChar, tempBuffer, bufferSize-currentChar);
    } else if (mode != Preferences::PrintFloatMode::Engineering || Integer::NumberOfBase10DigitsWithoutSign(m) > minimalNumberOfMantissaDigits) {
      /* Forward one or more chars: _
       * Write the mantissa _23456
       * Copy the most significant digits on the forwarded chars: 223456
       * Write the dot : 2.3456
       *
       * We should use the UTF8Helper to manipulate chars, but it is clearer to
       * manipulate chars directly, so we just put assumptions on the char size
       * of the code points we manipulate. */
      assert(UTF8Decoder::CharSizeOfCodePoint('.') == 1);
      currentChar++;
      if (currentChar >= bufferSize-1) { return bufferSize-1; }
      int decimalMarkerPosition = currentChar + (mode == Preferences::PrintFloatMode::Engineering ? minimalNumberOfMantissaDigits - 1 : 0);
      currentChar += strlcpy(buffer+currentChar, tempBuffer, bufferSize-currentChar);
      assert(UTF8Decoder::CharSizeOfCodePoint(buffer[decimalMarkerPosition]) == 1);
      int numberOfCharsToShift = (mode == Preferences::PrintFloatMode::Engineering ? minimalNumberOfMantissaDigits : 1);
      for (int i = 0; i < numberOfCharsToShift; i++) {
        int charIndex = decimalMarkerPosition - numberOfCharsToShift + i;
        buffer[charIndex] = buffer[charIndex+1];
      }
      buffer[decimalMarkerPosition] = '.';
    } else {
      currentChar += strlcpy(buffer+currentChar, tempBuffer, bufferSize-currentChar);
    }
    if (currentChar >= bufferSize-1) { return bufferSize-1; }
    if ((mode == Preferences::PrintFloatMode::Engineering && exponentForEngineeringNotation == 0) || exponent == 0) {
      return currentChar;
    }
    currentChar += SerializationHelper::CodePoint(buffer + currentChar, bufferSize - currentChar, UCodePointLatinLetterSmallCapitalE);
    if (currentChar >= bufferSize-1) { return bufferSize-1; }
    if (mode == Preferences::PrintFloatMode::Engineering) {
      currentChar += Integer(exponentForEngineeringNotation).serialize(buffer+currentChar, bufferSize-currentChar);
    } else {
      currentChar += Integer(exponent).serialize(buffer+currentChar, bufferSize-currentChar);
    }
    return currentChar;
  }
  // Case 3: Decimal mode
  assert(UTF8Decoder::CharSizeOfCodePoint('.') == 1);
  assert(UTF8Decoder::CharSizeOfCodePoint('0') == 1);
  int deltaCharMantissa = exponent < 0 ? -exponent+1 : 0;
  strlcpy(buffer+currentChar+deltaCharMantissa, tempBuffer, std::max(0, bufferSize-deltaCharMantissa-currentChar));
  if (exponent < 0) {
    for (int i = 0; i <= -exponent; i++) {
      buffer[currentChar++] = i == 1 ? '.' : '0';
      if (currentChar >= bufferSize-1) { return bufferSize-1; }
    }
  }
  currentChar += mantissaLength;
  if (currentChar >= bufferSize - 1) { return bufferSize-1; } // Check if strlcpy returned prematurely
  if (exponent >= 0 && exponent < mantissaLength-1) {
    if (currentChar+1 >= bufferSize-1) { return bufferSize-1; }
    int decimalMarkerPosition = m_negative ? exponent + 1 : exponent;
    for (int i = currentChar-1; i > decimalMarkerPosition; i--) {
      buffer[i+1] = buffer[i];
    }
    if (currentChar >= bufferSize-1) { return bufferSize-1; }
    assert(UTF8Decoder::CharSizeOfCodePoint('.') == 1);
    buffer[decimalMarkerPosition+1] = '.';
    currentChar++;
  }
  if (currentChar+1 >= bufferSize-1) { return bufferSize-1; }
  if (exponent >= 0 && exponent > mantissaLength-1) {
    int endMarkerPosition = m_negative ? exponent+1 : exponent;
    for (int i = currentChar-1; i < endMarkerPosition; i++) {
      currentChar += SerializationHelper::CodePoint(buffer + currentChar, bufferSize - currentChar, '0');
      if (currentChar+1 >= bufferSize-1) { return bufferSize-1; }
    }
  }
  buffer[currentChar] = 0;
  return currentChar;
}

template<typename T> T DecimalNode::templatedApproximate() const {
  Integer m = signedMantissa();
  T f = m.approximate<T>();
  int numberOfDigits = Integer::NumberOfBase10DigitsWithoutSign(m);
  return f*std::pow((T)10.0, (T)(m_exponent-numberOfDigits+1));
}

int Decimal::Exponent(const char * integralPart, int integralPartLength, const char * fractionalPart, int fractionalPartLength, const char * exponent, int exponentLength, bool exponentNegative) {
  if (exponentLength > 0 && UTF8Helper::CodePointIs(exponent, '-')) {
    exponent++;
    exponentNegative = true;
    exponentLength--;
  }
  int base = 10;
  int exp = 0;
  for (int i = 0; i < exponentLength; i++) {
    exp *= base;
    assert(*exponent >= '0' && *exponent <= '9');
    exp += *exponent-'0';
    exponent++;
  }
  if (exponentNegative) {
    exp = -exp;
  }
  const char * integralPartEnd = integralPart + integralPartLength;
  if (integralPart != nullptr) {
    while (*integralPart == '0' && integralPart < integralPartEnd) {
      integralPart++;
    }
  }
  exp += integralPartEnd-integralPart-1;
  if (integralPart == integralPartEnd) {
    const char * fractionalPartEnd = fractionalPart + fractionalPartLength;
    if (fractionalPart != nullptr) {
      while (UTF8Helper::CodePointIs(fractionalPart, '0') && fractionalPart < fractionalPartEnd) {
        fractionalPart++;
        exp--;
      }
    }
    if (fractionalPart == fractionalPartEnd) {
      exp += fractionalPartLength+1;
    }
  }
  return exp;
}

Decimal Decimal::Builder(const char * integralPart, int integralPartLength, const char * fractionalPart, int fractionalPartLength, int exponent) {
  /* Create a Decimal whose mantissa has less than
   * k_numberOfStoredSignificantDigits. We round exceeding number if necessary. */
  Integer zero(0);
  Integer base(10);
  // Get rid of useless preceeding 0s
  while (UTF8Helper::CodePointIs(integralPart, '0') && integralPartLength > 1) {
    integralPart++;
    integralPartLength--;
  }
  //TODO: set a FLAG to tell that a rounding happened?
  bool rounding = integralPartLength > PrintFloat::k_numberOfStoredSignificantDigits && integralPart[PrintFloat::k_numberOfStoredSignificantDigits] >= '5';
  /* At this point, the exponent has already been computed. In the very special
   * case where all the significant digits of the mantissa are 9, rounding up
   * must increment the exponent. For instance, rounding up 0.99...9 (whose
   * exponent is -1) yields 1 (whose exponent is 0). To that end, the
   * significant digits will be scanned successively to determine whether the
   * exponent should be incremented. */
  bool incrementExponentAfterRoundingUp = true;
  // Cap the length of the integralPart
  integralPartLength = integralPartLength > PrintFloat::k_numberOfStoredSignificantDigits ? PrintFloat::k_numberOfStoredSignificantDigits : integralPartLength;
  Integer numerator(integralPart, integralPartLength, false);
  assert(!numerator.isOverflow());
  // Special case for 0.??? : get rid of useless 0s in front of the integralPartLength
  if (fractionalPart != nullptr && integralPartLength == 1 && UTF8Helper::CodePointIs(integralPart, '0')) {
    integralPartLength = 0;
    while (UTF8Helper::CodePointIs(fractionalPart, '0')) {
      fractionalPart++;
      fractionalPartLength--;
    }
  }
  rounding |= fractionalPart && integralPartLength+fractionalPartLength > PrintFloat::k_numberOfStoredSignificantDigits && fractionalPart[PrintFloat::k_numberOfStoredSignificantDigits-integralPartLength] >= '5';
  fractionalPartLength = integralPartLength+fractionalPartLength > PrintFloat::k_numberOfStoredSignificantDigits ? PrintFloat::k_numberOfStoredSignificantDigits - integralPartLength : fractionalPartLength;
  while (incrementExponentAfterRoundingUp && integralPartLength-- > 0) {
    incrementExponentAfterRoundingUp = (*(integralPart++) == '9');
  }
  for (int i = 0; i < fractionalPartLength; i++) {
    assert(fractionalPart);
    numerator = Integer::Multiplication(numerator, base);
    assert(*fractionalPart >= '0' && *fractionalPart <= '9');
    numerator = Integer::Addition(numerator, Integer(*fractionalPart-'0'));
    incrementExponentAfterRoundingUp &= (*fractionalPart == '9');
    fractionalPart++;
  }
  if (rounding) {
    numerator = Integer::Addition(numerator, Integer(1));
    if (incrementExponentAfterRoundingUp) {
      exponent++;
    }
  }
  exponent = numerator.isZero() ? 0 : exponent;
  return Decimal::Builder(numerator, exponent);
}

template <typename T>
Decimal Decimal::Builder(T f) {
  assert(!std::isnan(f) && !std::isinf(f));
  int exp = IEEE754<T>::exponentBase10(f);
  int numberOfSignificantDigits = sizeof(T) == sizeof(float) ? PrintFloat::k_numberOfPrintedSignificantDigits : PrintFloat::k_numberOfStoredSignificantDigits;
#ifdef PLATFORM_ESP32
  // ESP32 Xtensa toolchain has broken 64-bit shift/mask operations in
  // Integer(int64_t), so cap mantissa to 9 digits to fit in int32_t.
  if (numberOfSignificantDigits > 9) {
    numberOfSignificantDigits = 9;
  }
#endif
  double mantissaf = f * std::pow(10.0, (double)(-exp));
  mantissaf = mantissaf * std::pow((double)10.0, (double)(numberOfSignificantDigits-1));
  double biggestMantissa = std::pow((double)10.0, (double)(numberOfSignificantDigits))-0.5;
  if (std::fabs(mantissaf) >= biggestMantissa) {
    exp++;
  }
#ifdef PLATFORM_ESP32
  int32_t mantissaI = (int32_t)(std::round(mantissaf));
  // Remove trailing zeros manually — removeZeroAtTheEnd corrupts Integers on ESP32
  // due to Xtensa toolchain bugs with struct member copy in assignment operators
  while (mantissaI != 0 && mantissaI % 10 == 0) {
    mantissaI /= 10;
  }
  Integer m = Integer((native_int_t)mantissaI);
#else
  Integer m = Integer((int64_t)(std::round(mantissaf)));
  removeZeroAtTheEnd(&m);
#endif
  return Decimal::Builder(m, exp);
}

/* We do not get rid of the useless 0s ending the mantissa here because we want
 * to keep them if they were entered by the user. */
Decimal Decimal::Builder(Integer m, int e) {
#ifdef PLATFORM_ESP32
  Serial.printf("[DEC.BV] BY-VALUE copy: nDigits=%d digits[0]=0x%08x id=0x%04x\n",
                m.numberOfDigits(), m.numberOfDigits() > 0 ? m.digits()[0] : 0, m.identifier());
  Serial.flush();
#endif
  return Decimal::Builder(DecimalSize(m.numberOfDigits()), m, e);
}

Decimal Decimal::Builder(size_t size, const Integer & m, int e) {
  void * bufferNode = TreePool::sharedPool()->alloc(size);
#ifdef PLATFORM_ESP32
  Serial.printf("[DEC.Build] alloc=%p size=%d nDig=%d exp=%d neg=%d\n", bufferNode, (int)size, m.numberOfDigits(), e, m.isNegative());
  if (m.numberOfDigits() > 0) {
    const native_uint_t * d = m.digits();
    Serial.printf("[DEC.Build] src digits:");
    for (int _i = 0; _i < m.numberOfDigits() && _i < 4; _i++) {
      Serial.printf(" [%d]=0x%08x", _i, d[_i]);
    }
    Serial.printf("\n");
  }
  Serial.flush();
#endif
  DecimalNode * node = new (bufferNode) DecimalNode(m.digits(), m.numberOfDigits(), e, m.isNegative());
#ifdef PLATFORM_ESP32
  {
    uint32_t * raw = reinterpret_cast<uint32_t *>(node);
    Serial.printf("[DEC.Build] node raw: %08x %08x %08x %08x %08x %08x %08x\n",
                  raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6]);
    Serial.flush();
  }
#endif
  TreeHandle h = TreeHandle::BuildWithGhostChildren(node);
  return static_cast<Decimal &>(h);
}

Expression Decimal::setSign(ExpressionNode::Sign s) {
  assert(s == ExpressionNode::Sign::Positive || s == ExpressionNode::Sign::Negative);
  Decimal result = *this;
  result.node()->setNegative(s == ExpressionNode::Sign::Negative);
  return std::move(result);
}

Expression Decimal::shallowReduce() {
  int exp = node()->exponent();
  Integer numerator = node()->signedMantissa();
  /* To avoid uselessly big numerator and denominator, we get rid of useless 0s
   * ending the mantissa before reducing to Rational. */
  removeZeroAtTheEnd(&numerator);
  int numberOfDigits = Integer::NumberOfBase10DigitsWithoutSign(numerator);
  Integer denominator(1);
  if (exp >= numberOfDigits-1) {
    numerator = Integer::Multiplication(numerator, Integer::Power(Integer(10), Integer(exp-numberOfDigits+1)));
  } else {
    denominator = Integer::Power(Integer(10), Integer(numberOfDigits-1-exp));
  }
  Expression result;
  if (numerator.isOverflow() || denominator.isOverflow()) {
    result = Number::FloatNumber(node()->signedMantissa().template approximate<double>()*std::pow(10.0, (double)exp));
  } else {
    result = Rational::Builder(numerator, denominator);
  }
  replaceWithInPlace(result);
  return result;
}

Expression Decimal::shallowBeautify() {
  if (sign() == ExpressionNode::Sign::Negative) {
    Expression abs = setSign(ExpressionNode::Sign::Positive);
    Opposite o = Opposite::Builder();
    replaceWithInPlace(o);
    o.replaceChildAtIndexInPlace(0, abs);
    return std::move(o);
  }
  return *this;
}

template Decimal Decimal::Decimal::Builder(double);
template Decimal Decimal::Decimal::Builder(float);

}
