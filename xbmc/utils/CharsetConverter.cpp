/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "CharsetConverter.h"
#include "Util.h"
#include "utils/StringUtils.h"
#include <fribidi/fribidi.h>
#include "LangInfo.h"
#include "guilib/LocalizeStrings.h"
#include "settings/Setting.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#include "log.h"

#include <errno.h>
#include <iconv.h>

#if !defined(TARGET_WINDOWS) && defined(HAVE_CONFIG_H)
  #include "config.h"
#endif

#ifdef WORDS_BIGENDIAN
  #define ENDIAN_SUFFIX "BE"
#else
  #define ENDIAN_SUFFIX "LE"
#endif

#if defined(TARGET_DARWIN)
  #define WCHAR_IS_UCS_4 1
  #define UTF16_CHARSET "UTF-16" ENDIAN_SUFFIX
  #define UTF32_CHARSET "UTF-32" ENDIAN_SUFFIX
  #define UTF8_SOURCE "UTF-8-MAC"
  #define WCHAR_CHARSET UTF32_CHARSET
#elif defined(TARGET_WINDOWS)
  #define WCHAR_IS_UTF16 1
  #define UTF16_CHARSET "UTF-16" ENDIAN_SUFFIX
  #define UTF32_CHARSET "UTF-32" ENDIAN_SUFFIX
  #define UTF8_SOURCE "UTF-8"
  #define WCHAR_CHARSET UTF16_CHARSET 
  #pragma comment(lib, "libfribidi.lib")
  #pragma comment(lib, "libiconv.lib")
#elif defined(TARGET_ANDROID)
  #define WCHAR_IS_UCS_4 1
  #define UTF16_CHARSET "UTF-16" ENDIAN_SUFFIX
  #define UTF32_CHARSET "UTF-32" ENDIAN_SUFFIX
  #define UTF8_SOURCE "UTF-8"
  #define WCHAR_CHARSET UTF32_CHARSET 
#else
  #define UTF16_CHARSET "UTF-16" ENDIAN_SUFFIX
  #define UTF32_CHARSET "UTF-32" ENDIAN_SUFFIX
  #define UTF8_SOURCE "UTF-8"
  #define WCHAR_CHARSET "WCHAR_T"
  #if __STDC_ISO_10646__
    #ifdef SIZEOF_WCHAR_T
      #if SIZEOF_WCHAR_T == 4
        #define WCHAR_IS_UCS_4 1
      #elif SIZEOF_WCHAR_T == 2
        #define WCHAR_IS_UCS_2 1
      #endif
    #endif
  #endif
#endif

#define NO_ICONV ((iconv_t)-1)

enum SpecialCharset
{
  NotSpecialCharset = 0,
  SystemCharset,
  UserCharset /* locale.charset */, 
  SubtitleCharset /* subtitles.charset */,
  KaraokeCharset /* karaoke.charset */
};


class CConverterType : public CCriticalSection
{
public:
  CConverterType(const std::string&  sourceCharset,        const std::string&  targetCharset,        unsigned int targetSingleCharMaxLen = 1);
  CConverterType(enum SpecialCharset sourceSpecialCharset, const std::string&  targetCharset,        unsigned int targetSingleCharMaxLen = 1);
  CConverterType(const std::string&  sourceCharset,        enum SpecialCharset targetSpecialCharset, unsigned int targetSingleCharMaxLen = 1);
  CConverterType(enum SpecialCharset sourceSpecialCharset, enum SpecialCharset targetSpecialCharset, unsigned int targetSingleCharMaxLen = 1);
  CConverterType(const CConverterType& other);
  ~CConverterType();

  iconv_t GetConverter(CSingleLock& converterLock);

  void Reset(void);
  void ReinitTo(const std::string& sourceCharset, const std::string& targetCharset, unsigned int targetSingleCharMaxLen = 1);
  std::string GetSourceCharset(void) const  { return m_sourceCharset; }
  std::string GetTargetCharset(void) const  { return m_targetCharset; }
  unsigned int GetTargetSingleCharMaxLen(void) const  { return m_targetSingleCharMaxLen; }

private:
  static std::string ResolveSpecialCharset(enum SpecialCharset charset);

  enum SpecialCharset m_sourceSpecialCharset;
  std::string         m_sourceCharset;
  enum SpecialCharset m_targetSpecialCharset;
  std::string         m_targetCharset;
  iconv_t             m_iconv;
  unsigned int        m_targetSingleCharMaxLen;
};

CConverterType::CConverterType(const std::string& sourceCharset, const std::string& targetCharset, unsigned int targetSingleCharMaxLen /*= 1*/) : CCriticalSection(),
  m_sourceSpecialCharset(NotSpecialCharset),
  m_sourceCharset(sourceCharset),
  m_targetSpecialCharset(NotSpecialCharset),
  m_targetCharset(targetCharset),
  m_iconv(NO_ICONV),
  m_targetSingleCharMaxLen(targetSingleCharMaxLen)
{
}

CConverterType::CConverterType(enum SpecialCharset sourceSpecialCharset, const std::string& targetCharset, unsigned int targetSingleCharMaxLen /*= 1*/) : CCriticalSection(),
  m_sourceSpecialCharset(sourceSpecialCharset),
  m_sourceCharset(),
  m_targetSpecialCharset(NotSpecialCharset),
  m_targetCharset(targetCharset),
  m_iconv(NO_ICONV),
  m_targetSingleCharMaxLen(targetSingleCharMaxLen)
{
}

CConverterType::CConverterType(const std::string& sourceCharset, enum SpecialCharset targetSpecialCharset, unsigned int targetSingleCharMaxLen /*= 1*/) : CCriticalSection(),
  m_sourceSpecialCharset(NotSpecialCharset),
  m_sourceCharset(sourceCharset),
  m_targetSpecialCharset(targetSpecialCharset),
  m_targetCharset(),
  m_iconv(NO_ICONV),
  m_targetSingleCharMaxLen(targetSingleCharMaxLen)
{
}

CConverterType::CConverterType(enum SpecialCharset sourceSpecialCharset, enum SpecialCharset targetSpecialCharset, unsigned int targetSingleCharMaxLen /*= 1*/) : CCriticalSection(),
  m_sourceSpecialCharset(sourceSpecialCharset),
  m_sourceCharset(),
  m_targetSpecialCharset(targetSpecialCharset),
  m_targetCharset(),
  m_iconv(NO_ICONV),
  m_targetSingleCharMaxLen(targetSingleCharMaxLen)
{
}

CConverterType::CConverterType(const CConverterType& other) : CCriticalSection(),
  m_sourceSpecialCharset(other.m_sourceSpecialCharset),
  m_sourceCharset(other.m_sourceCharset),
  m_targetSpecialCharset(other.m_targetSpecialCharset),
  m_targetCharset(other.m_targetCharset),
  m_iconv(NO_ICONV),
  m_targetSingleCharMaxLen(other.m_targetSingleCharMaxLen)
{
}


CConverterType::~CConverterType()
{
  CSingleLock(*this);
  if (m_iconv != NO_ICONV)
    iconv_close(m_iconv);
}


iconv_t CConverterType::GetConverter(CSingleLock& converterLock)
{
  // ensure that this unique instance is locked externally
  if (&converterLock.get_underlying() != this)
    return NO_ICONV;

  if (m_iconv == NO_ICONV)
  {
    if (m_sourceSpecialCharset)
      m_sourceCharset = ResolveSpecialCharset(m_sourceSpecialCharset);
    if (m_targetSpecialCharset)
      m_targetCharset = ResolveSpecialCharset(m_targetSpecialCharset);

    m_iconv = iconv_open(m_targetCharset.c_str(), m_sourceCharset.c_str());
    
    if (m_iconv == NO_ICONV)
      CLog::Log(LOGERROR, "%s: iconv_open() for \"%s\" -> \"%s\" failed, errno = %d (%s)",
                __FUNCTION__, m_sourceCharset.c_str(), m_targetCharset.c_str(), errno, strerror(errno));
  }

  return m_iconv;
}


void CConverterType::Reset(void)
{
  CSingleLock(*this);
  if (m_iconv != NO_ICONV)
  {
    iconv_close(m_iconv);
    m_iconv = NO_ICONV;
  }

  if (m_sourceSpecialCharset)
    m_sourceCharset.clear();
  if (m_targetSpecialCharset)
    m_targetCharset.clear();

}

void CConverterType::ReinitTo(const std::string& sourceCharset, const std::string& targetCharset, unsigned int targetSingleCharMaxLen /*= 1*/)
{
  CSingleLock(*this);
  if (sourceCharset != m_sourceCharset || targetCharset != m_targetCharset)
  {
    if (m_iconv != NO_ICONV)
    {
      iconv_close(m_iconv);
      m_iconv = NO_ICONV;
    }

    m_sourceSpecialCharset = NotSpecialCharset;
    m_sourceCharset = sourceCharset;
    m_targetSpecialCharset = NotSpecialCharset;
    m_targetCharset = targetCharset;
    m_targetSingleCharMaxLen = targetSingleCharMaxLen;
  }
}

std::string CConverterType::ResolveSpecialCharset(enum SpecialCharset charset)
{
  switch (charset)
  {
  case SystemCharset:
    return "";
  case UserCharset:
    return g_langInfo.GetGuiCharSet();
  case SubtitleCharset:
    return g_langInfo.GetSubtitleCharSet();
  case KaraokeCharset:
    {
      CSetting* karaokeSetting = CSettings::Get().GetSetting("karaoke.charset");
      if (karaokeSetting == NULL || ((CSettingString*)karaokeSetting)->GetValue() == "DEFAULT")
        return g_langInfo.GetGuiCharSet();

      return ((CSettingString*)karaokeSetting)->GetValue();
    }
  case NotSpecialCharset:
  default:
    return "UTF-8"; /* dummy value */
  }
}


enum StdConversionType /* Keep it in sync with CCharsetConverter::CInnerConverter::m_stdConversion */
{
  NoConversion = -1,
  Utf8ToUtf32 = 0,
  Utf32ToUtf8,
  Utf32ToW,
  WToUtf32,
  SubtitleCharsetToW,
  Utf8ToUserCharset,
  UserCharsetToUtf8,
  Utf32ToUserCharset,
  WtoUtf8,
  Utf16LEtoW,
  Utf16BEtoUtf8,
  Utf16LEtoUtf8,
  Utf8toW,
  Utf8ToSystem,
  Ucs2CharsetToUtf8,
  NumberOfStdConversionTypes /* Dummy sentinel entry */
};


/* We don't want to pollute header file with many additional includes and definitions, so put 
   here all staff that require usage of types defined in this file or in additional headers */
class CCharsetConverter::CInnerConverter
{
public:
  static bool logicalToVisualBiDi(const std::string& stringSrc, std::string& stringDst, FriBidiCharSet fribidiCharset, FriBidiCharType base = FRIBIDI_TYPE_LTR, bool* bWasFlipped =NULL);
  
  template<class INPUT,class OUTPUT>
  static bool stdConvert(StdConversionType convertType, const INPUT& strSource, OUTPUT& strDest, bool failOnInvalidChar = false);
  template<class INPUT,class OUTPUT>
  static bool customConvert(const std::string& sourceCharset, const std::string& targetCharset, const INPUT& strSource, OUTPUT& strDest, bool failOnInvalidChar = false);

  template<class INPUT,class OUTPUT>
  static bool convert(iconv_t type, int multiplier, const INPUT& strSource, OUTPUT& strDest, bool failOnInvalidChar = false);

  static CConverterType m_stdConversion[NumberOfStdConversionTypes];
};

/* single symbol sizes in chars */
const int CCharsetConverter::m_Utf8CharMinSize = 1;
const int CCharsetConverter::m_Utf8CharMaxSize = 4;

CConverterType CCharsetConverter::CInnerConverter::m_stdConversion[NumberOfStdConversionTypes] = /* keep it in sync with enum StdConversionType */
{
  /* Utf8ToUtf32 */         CConverterType(UTF8_SOURCE,     UTF32_CHARSET),
  /* Utf32ToUtf8 */         CConverterType(UTF32_CHARSET,   "UTF-8", CCharsetConverter::m_Utf8CharMaxSize),
  /* Utf32ToW */            CConverterType(UTF32_CHARSET,   WCHAR_CHARSET),
  /* WToUtf32 */            CConverterType(WCHAR_CHARSET,   UTF32_CHARSET),
  /* SubtitleCharsetToW */  CConverterType(SubtitleCharset, WCHAR_CHARSET),
  /* Utf8ToUserCharset */   CConverterType(UTF8_SOURCE,     UserCharset),
  /* UserCharsetToUtf8 */   CConverterType(UserCharset,     "UTF-8", CCharsetConverter::m_Utf8CharMaxSize),
  /* Utf32ToUserCharset */  CConverterType(UTF32_CHARSET,   UserCharset),
  /* WtoUtf8 */             CConverterType(WCHAR_CHARSET,   "UTF-8", CCharsetConverter::m_Utf8CharMaxSize),
  /* Utf16LEtoW */          CConverterType("UTF-16LE",      WCHAR_CHARSET),
  /* Utf16BEtoUtf8 */       CConverterType("UTF-16BE",      "UTF-8", CCharsetConverter::m_Utf8CharMaxSize),
  /* Utf16LEtoUtf8 */       CConverterType("UTF-16LE",      "UTF-8", CCharsetConverter::m_Utf8CharMaxSize),
  /* Utf8toW */             CConverterType(UTF8_SOURCE,     WCHAR_CHARSET),
  /* Utf8ToSystem */        CConverterType(UTF8_SOURCE,     SystemCharset),
  /* Ucs2CharsetToUtf8 */   CConverterType("UCS-2LE",       "UTF-8", CCharsetConverter::m_Utf8CharMaxSize)
};



template<class INPUT,class OUTPUT>
bool CCharsetConverter::CInnerConverter::stdConvert(StdConversionType convertType, const INPUT& strSource, OUTPUT& strDest, bool failOnInvalidChar /*= false*/)
{
  strDest.clear();
  if (strSource.empty())
    return true;

  if (convertType < 0 || convertType >= NumberOfStdConversionTypes)
    return false;

  CConverterType& convType = m_stdConversion[convertType];
  CSingleLock converterLock(convType);

  return convert(convType.GetConverter(converterLock), convType.GetTargetSingleCharMaxLen(), strSource, strDest, failOnInvalidChar);
}

template<class INPUT,class OUTPUT>
bool CCharsetConverter::CInnerConverter::customConvert(const std::string& sourceCharset, const std::string& targetCharset, const INPUT& strSource, OUTPUT& strDest, bool failOnInvalidChar /*= false*/)
{
  strDest.clear();
  if (strSource.empty())
    return true;

  iconv_t conv = iconv_open(targetCharset.c_str(), sourceCharset.c_str());
  if (conv == NO_ICONV)
  {
    CLog::Log(LOGERROR, "%s: iconv_open() for \"%s\" -> \"%s\" failed, errno = %d (%s)",
              __FUNCTION__, sourceCharset.c_str(), targetCharset.c_str(), errno, strerror(errno));
    return false;
  }
  const int dstMultp = (targetCharset.compare(0, 5, "UTF-8") == 0) ? CCharsetConverter::m_Utf8CharMaxSize : 1;
  const bool result = convert(conv, dstMultp, strSource, strDest, failOnInvalidChar);
  iconv_close(conv);

  return result;
}


#if defined(FRIBIDI_CHAR_SET_NOT_FOUND)
static FriBidiCharSet m_stringFribidiCharset     = FRIBIDI_CHAR_SET_NOT_FOUND;
#define FRIBIDI_UTF8 FRIBIDI_CHAR_SET_UTF8
#define FRIBIDI_NOTFOUND FRIBIDI_CHAR_SET_NOT_FOUND
#else /* compatibility to older version */
static FriBidiCharSet m_stringFribidiCharset     = FRIBIDI_CHARSET_NOT_FOUND;
#define FRIBIDI_UTF8 FRIBIDI_CHARSET_UTF8
#define FRIBIDI_NOTFOUND FRIBIDI_CHARSET_NOT_FOUND
#endif

static CCriticalSection            m_critSection;

static struct SFribidMapping
{
  FriBidiCharSet name;
  const char*    charset;
} g_fribidi[] = {
#if defined(FRIBIDI_CHAR_SET_NOT_FOUND)
  { FRIBIDI_CHAR_SET_ISO8859_6, "ISO-8859-6"   }
, { FRIBIDI_CHAR_SET_ISO8859_8, "ISO-8859-8"   }
, { FRIBIDI_CHAR_SET_CP1255   , "CP1255"       }
, { FRIBIDI_CHAR_SET_CP1255   , "Windows-1255" }
, { FRIBIDI_CHAR_SET_CP1256   , "CP1256"       }
, { FRIBIDI_CHAR_SET_CP1256   , "Windows-1256" }
, { FRIBIDI_CHAR_SET_NOT_FOUND, NULL           }
#else /* compatibility to older version */
  { FRIBIDI_CHARSET_ISO8859_6, "ISO-8859-6"   }
, { FRIBIDI_CHARSET_ISO8859_8, "ISO-8859-8"   }
, { FRIBIDI_CHARSET_CP1255   , "CP1255"       }
, { FRIBIDI_CHARSET_CP1255   , "Windows-1255" }
, { FRIBIDI_CHARSET_CP1256   , "CP1256"       }
, { FRIBIDI_CHARSET_CP1256   , "Windows-1256" }
, { FRIBIDI_CHARSET_NOT_FOUND, NULL           }
#endif
};

static struct SCharsetMapping
{
  const char* charset;
  const char* caption;
} g_charsets[] = {
   { "ISO-8859-1", "Western Europe (ISO)" }
 , { "ISO-8859-2", "Central Europe (ISO)" }
 , { "ISO-8859-3", "South Europe (ISO)"   }
 , { "ISO-8859-4", "Baltic (ISO)"         }
 , { "ISO-8859-5", "Cyrillic (ISO)"       }
 , { "ISO-8859-6", "Arabic (ISO)"         }
 , { "ISO-8859-7", "Greek (ISO)"          }
 , { "ISO-8859-8", "Hebrew (ISO)"         }
 , { "ISO-8859-9", "Turkish (ISO)"        }
 , { "CP1250"    , "Central Europe (Windows)" }
 , { "CP1251"    , "Cyrillic (Windows)"       }
 , { "CP1252"    , "Western Europe (Windows)" }
 , { "CP1253"    , "Greek (Windows)"          }
 , { "CP1254"    , "Turkish (Windows)"        }
 , { "CP1255"    , "Hebrew (Windows)"         }
 , { "CP1256"    , "Arabic (Windows)"         }
 , { "CP1257"    , "Baltic (Windows)"         }
 , { "CP1258"    , "Vietnamesse (Windows)"    }
 , { "CP874"     , "Thai (Windows)"           }
 , { "BIG5"      , "Chinese Traditional (Big5)" }
 , { "GBK"       , "Chinese Simplified (GBK)" }
 , { "SHIFT_JIS" , "Japanese (Shift-JIS)"     }
 , { "CP949"     , "Korean"                   }
 , { "BIG5-HKSCS", "Hong Kong (Big5-HKSCS)"   }
 , { NULL        , NULL                       }
};


#define ICONV_PREPARE(iconv) iconv=(iconv_t)-1
#define ICONV_SAFE_CLOSE(iconv) if (iconv!=(iconv_t)-1) { iconv_close(iconv); iconv=(iconv_t)-1; }

size_t iconv_const (void* cd, const char** inbuf, size_t* inbytesleft,
                    char** outbuf, size_t* outbytesleft)
{
    struct iconv_param_adapter {
        iconv_param_adapter(const char**p) : p(p) {}
        iconv_param_adapter(char**p) : p((const char**)p) {}
        operator char**() const
        {
            return(char**)p;
        }
        operator const char**() const
        {
            return(const char**)p;
        }
        const char** p;
    };

    return iconv((iconv_t)cd, iconv_param_adapter(inbuf), inbytesleft, outbuf, outbytesleft);
}

template<class INPUT,class OUTPUT>
bool CCharsetConverter::CInnerConverter::convert(iconv_t type, int multiplier, const INPUT& strSource, OUTPUT& strDest, bool failOnInvalidChar /*= false*/)
{
  if (type == NO_ICONV)
    return false;

  //input buffer for iconv() is the buffer from strSource
  size_t      inBufSize  = (strSource.length() + 1) * sizeof(typename INPUT::value_type);
  const char* inBuf      = (const char*)strSource.c_str();

  //allocate output buffer for iconv()
  size_t      outBufSize = (strSource.length() + 1) * sizeof(typename OUTPUT::value_type) * multiplier;
  char*       outBuf     = (char*)malloc(outBufSize);
  if (outBuf == NULL)
  {
      CLog::Log(LOGSEVERE, "%s: malloc failed", __FUNCTION__);
      return false;
  }

  size_t      inBytesAvail  = inBufSize;  //how many bytes iconv() can read
  size_t      outBytesAvail = outBufSize; //how many bytes iconv() can write
  const char* inBufStart    = inBuf;      //where in our input buffer iconv() should start reading
  char*       outBufStart   = outBuf;     //where in out output buffer iconv() should start writing

  size_t returnV;
  while(1)
  {
    //iconv() will update inBufStart, inBytesAvail, outBufStart and outBytesAvail
    returnV = iconv_const(type, &inBufStart, &inBytesAvail, &outBufStart, &outBytesAvail);

    if (returnV == (size_t)-1)
    {
      if (errno == E2BIG) //output buffer is not big enough
      {
        //save where iconv() ended converting, realloc might make outBufStart invalid
        size_t bytesConverted = outBufSize - outBytesAvail;

        //make buffer twice as big
        outBufSize   *= 2;
        char* newBuf  = (char*)realloc(outBuf, outBufSize);
        if (!newBuf)
        {
          CLog::Log(LOGSEVERE, "%s realloc failed with errno=%d(%s)",
                    __FUNCTION__, errno, strerror(errno));
          break;
        }
        outBuf = newBuf;

        //update the buffer pointer and counter
        outBufStart   = outBuf + bytesConverted;
        outBytesAvail = outBufSize - bytesConverted;

        //continue in the loop and convert the rest
        continue;
      }
      else if (errno == EILSEQ) //An invalid multibyte sequence has been encountered in the input
      {
        if (failOnInvalidChar)
          break;

        //skip invalid byte
        inBufStart++;
        inBytesAvail--;
        //continue in the loop and convert the rest
        continue;
      }
      else if (errno == EINVAL) /* Invalid sequence at the end of input buffer */
      {
        if (!failOnInvalidChar)
          returnV = 0; /* reset error status to use converted part */

        break;
      }
      else //iconv() had some other error
      {
        CLog::Log(LOGERROR, "%s: iconv() failed, errno=%d (%s)",
                  __FUNCTION__, errno, strerror(errno));
      }
    }
    break;
  }

  //complete the conversion (reset buffers), otherwise the current data will prefix the data on the next call
  if (iconv_const(type, NULL, NULL, &outBufStart, &outBytesAvail) == (size_t)-1)
    CLog::Log(LOGERROR, "%s failed cleanup errno=%d(%s)", __FUNCTION__, errno, strerror(errno));

  if (returnV == (size_t)-1)
  {
    free(outBuf);
    return false;
  }
  //we're done

  const typename OUTPUT::size_type sizeInChars = (typename OUTPUT::size_type) (outBufSize - outBytesAvail) / sizeof(typename OUTPUT::value_type);
  typename OUTPUT::const_pointer strPtr = (typename OUTPUT::const_pointer) outBuf;
  /* Make sure that all buffer is assigned and string is stopped at end of buffer */
  if (strPtr[sizeInChars-1] == 0 && strSource[strSource.length()-1] != 0)
    strDest.assign(strPtr, sizeInChars-1);
  else
    strDest.assign(strPtr, sizeInChars);

  free(outBuf);

  return true;
}

using namespace std;

bool CCharsetConverter::CInnerConverter::logicalToVisualBiDi(const std::string& stringSrc, std::string& stringDst, FriBidiCharSet fribidiCharset, FriBidiCharType base /*= FRIBIDI_TYPE_LTR*/, bool* bWasFlipped /*=NULL*/)
{
  stringDst.clear();
  vector<std::string> lines = StringUtils::Split(stringSrc, "\n");

  if (bWasFlipped)
    *bWasFlipped = false;

  // libfribidi is not threadsafe, so make sure we make it so
  CSingleLock lock(m_critSection);

  const size_t numLines = lines.size();
  for (size_t i = 0; i < numLines; i++)
  {
    int sourceLen = lines[i].length();
    if (sourceLen == 0)
      continue;

    // Convert from the selected charset to Unicode
    FriBidiChar* logical = (FriBidiChar*) malloc((sourceLen + 1) * sizeof(FriBidiChar));
    if (logical == NULL)
    {
      CLog::Log(LOGSEVERE, "%s: can't allocate memory", __FUNCTION__);
      return false;
    }
    int len = fribidi_charset_to_unicode(fribidiCharset, (char*) lines[i].c_str(), sourceLen, logical);

    FriBidiChar* visual = (FriBidiChar*) malloc((len + 1) * sizeof(FriBidiChar));
    FriBidiLevel* levels = (FriBidiLevel*) malloc((len + 1) * sizeof(FriBidiLevel));
    if (levels == NULL || visual == NULL)
    {
      free(logical);
      free(visual);
      free(levels);
      CLog::Log(LOGSEVERE, "%s: can't allocate memory", __FUNCTION__);
      return false;
    }

    if (fribidi_log2vis(logical, len, &base, visual, NULL, NULL, levels))
    {
      // Removes bidirectional marks
      len = fribidi_remove_bidi_marks(visual, len, NULL, NULL, NULL);

      // Apperently a string can get longer during this transformation
      // so make sure we allocate the maximum possible character utf8
      // can generate atleast, should cover all bases
      char* result = new char[len*4];

      // Convert back from Unicode to the charset
      int len2 = fribidi_unicode_to_charset(fribidiCharset, visual, len, result);
      assert(len2 <= len*4);
      stringDst += result;
      delete[] result;

      // Check whether the string was flipped if one of the embedding levels is greater than 0
      if (bWasFlipped && !*bWasFlipped)
      {
        for (int i = 0; i < len; i++)
        {
          if ((int) levels[i] > 0)
          {
            *bWasFlipped = true;
            break;
          }
        }
      }
    }

    free(logical);
    free(visual);
    free(levels);
  }

  return true;
}

CCharsetConverter::CCharsetConverter()
{
}

void CCharsetConverter::OnSettingChanged(const CSetting* setting)
{
  if (setting == NULL)
    return;

  const std::string& settingId = setting->GetId();
  if (settingId == "locale.charset")
    resetUserCharset();
  else if (settingId == "subtitles.charset")
    resetSubtitleCharset();
  else if (settingId == "karaoke.charset")
    resetKaraokeCharset();
}

void CCharsetConverter::clear()
{
}

std::vector<std::string> CCharsetConverter::getCharsetLabels()
{
  vector<std::string> lab;
  for(SCharsetMapping* c = g_charsets; c->charset; c++)
    lab.push_back(c->caption);

  return lab;
}

std::string CCharsetConverter::getCharsetLabelByName(const std::string& charsetName)
{
  for(SCharsetMapping* c = g_charsets; c->charset; c++)
  {
    if (StringUtils::EqualsNoCase(charsetName,c->charset))
      return c->caption;
  }

  return "";
}

std::string CCharsetConverter::getCharsetNameByLabel(const std::string& charsetLabel)
{
  for(SCharsetMapping* c = g_charsets; c->charset; c++)
  {
    if (StringUtils::EqualsNoCase(charsetLabel, c->caption))
      return c->charset;
  }

  return "";
}

bool CCharsetConverter::isBidiCharset(const std::string& charset)
{
  for(SFribidMapping* c = g_fribidi; c->charset; c++)
  {
    if (StringUtils::EqualsNoCase(charset, c->charset))
      return true;
  }
  return false;
}

void CCharsetConverter::reset(void)
{
  for (int i = 0; i < NumberOfStdConversionTypes; i++)
    CInnerConverter::m_stdConversion[i].Reset();
}

void CCharsetConverter::resetSystemCharset(void)
{
  CInnerConverter::m_stdConversion[Utf8ToSystem].Reset();
}

void CCharsetConverter::resetUserCharset(void)
{
  CInnerConverter::m_stdConversion[UserCharsetToUtf8].Reset();
  CInnerConverter::m_stdConversion[UserCharsetToUtf8].Reset();
  CInnerConverter::m_stdConversion[Utf32ToUserCharset].Reset();
  resetSubtitleCharset();
  resetKaraokeCharset();
}

void CCharsetConverter::resetSubtitleCharset(void)
{
  CInnerConverter::m_stdConversion[SubtitleCharsetToW].Reset();
}

void CCharsetConverter::resetKaraokeCharset(void)
{
}

void CCharsetConverter::reinitCharsetsFromSettings(void)
{
  resetUserCharset(); // this will also reinit Subtitle and Karaoke charsets
}

bool CCharsetConverter::utf8ToUtf32(const std::string& utf8StringSrc, std::u32string& utf32StringDst, bool failOnBadChar /*= true*/)
{
  return CInnerConverter::stdConvert(Utf8ToUtf32, utf8StringSrc, utf32StringDst, failOnBadChar);
}

std::u32string CCharsetConverter::utf8ToUtf32(const std::string& utf8StringSrc, bool failOnBadChar /*= true*/)
{
  std::u32string converted;
  utf8ToUtf32(utf8StringSrc, converted, failOnBadChar);
  return converted;
}

bool CCharsetConverter::utf8ToUtf32Visual(const std::string& utf8StringSrc, std::u32string& utf32StringDst, bool bVisualBiDiFlip /*= false*/, bool forceLTRReadingOrder /*= false*/, bool failOnBadChar /*= false*/)
{
  if (bVisualBiDiFlip)
  {
    std::string strFlipped;
    if (!CInnerConverter::logicalToVisualBiDi(utf8StringSrc, strFlipped, FRIBIDI_UTF8, forceLTRReadingOrder ? FRIBIDI_TYPE_LTR : FRIBIDI_TYPE_PDF))
      return false;
    return CInnerConverter::stdConvert(Utf8ToUtf32, strFlipped, utf32StringDst, failOnBadChar);
  }
  return CInnerConverter::stdConvert(Utf8ToUtf32, utf8StringSrc, utf32StringDst, failOnBadChar);
}

bool CCharsetConverter::utf32ToUtf8(const std::u32string& utf32StringSrc, std::string& utf8StringDst, bool failOnBadChar /*= true*/)
{
  return CInnerConverter::stdConvert(Utf32ToUtf8, utf32StringSrc, utf8StringDst, failOnBadChar);
}

std::string CCharsetConverter::utf32ToUtf8(const std::u32string& utf32StringSrc, bool failOnBadChar /*= false*/)
{
  std::string converted;
  utf32ToUtf8(utf32StringSrc, converted, failOnBadChar);
  return converted;
}

bool CCharsetConverter::utf32ToW(const std::u32string& utf32StringSrc, std::wstring& wStringDst, bool failOnBadChar /*= true*/)
{
#ifdef WCHAR_IS_UCS_4
  wStringDst.assign((const wchar_t*)utf32StringSrc.c_str(), utf32StringSrc.length());
  return true;
#else // !WCHAR_IS_UCS_4
  return CInnerConverter::stdConvert(Utf32ToW, utf32StringSrc, wStringDst, failOnBadChar);
#endif // !WCHAR_IS_UCS_4
}

bool CCharsetConverter::utf32logicalToVisualBiDi(const std::u32string& logicalStringSrc, std::u32string& visualStringDst, bool forceLTRReadingOrder /*= false*/)
{
  visualStringDst.clear();
  std::string utf8Str;
  if (!utf32ToUtf8(logicalStringSrc, utf8Str, false))
    return false;

  return utf8ToUtf32Visual(utf8Str, visualStringDst, true, forceLTRReadingOrder);
}

bool CCharsetConverter::wToUtf32(const std::wstring& wStringSrc, std::u32string& utf32StringDst, bool failOnBadChar /*= true*/)
{
#ifdef WCHAR_IS_UCS_4
  /* UCS-4 is almost equal to UTF-32, but UTF-32 has strict limits on possible values, while UCS-4 is usually unchecked.
   * With this "conversion" we ensure that output will be valid UTF-32 string. */
#endif
  return CInnerConverter::stdConvert(WToUtf32, wStringSrc, utf32StringDst, failOnBadChar);
}

// The bVisualBiDiFlip forces a flip of characters for hebrew/arabic languages, only set to false if the flipping
// of the string is already made or the string is not displayed in the GUI
bool CCharsetConverter::utf8ToW(const std::string& utf8StringSrc, std::wstring& wStringDst, bool bVisualBiDiFlip /*= true*/, 
                                bool forceLTRReadingOrder /*= false*/, bool failOnBadChar /*= false*/, bool* bWasFlipped /*= NULL*/)
{
  // Try to flip hebrew/arabic characters, if any
  if (bVisualBiDiFlip)
  {
    std::string strFlipped;
    FriBidiCharType charset = forceLTRReadingOrder ? FRIBIDI_TYPE_LTR : FRIBIDI_TYPE_PDF;
    CInnerConverter::logicalToVisualBiDi(utf8StringSrc, strFlipped, FRIBIDI_UTF8, charset, bWasFlipped);
    return CInnerConverter::stdConvert(Utf8toW, strFlipped, wStringDst, failOnBadChar);
  }
  else
  {
    return CInnerConverter::stdConvert(Utf8toW, utf8StringSrc, wStringDst, failOnBadChar);
  }
}

bool CCharsetConverter::subtitleCharsetToW(const std::string& stringSrc, std::wstring& wStringDst)
{
  return CInnerConverter::stdConvert(SubtitleCharsetToW, stringSrc, wStringDst, false);
}

bool CCharsetConverter::fromW(const std::wstring& wStringSrc,
                              std::string& stringDst, const std::string& enc)
{
  return CInnerConverter::customConvert(WCHAR_CHARSET, enc, wStringSrc, stringDst);
}

bool CCharsetConverter::toW(const std::string& stringSrc,
                            std::wstring& wStringDst, const std::string& enc)
{
  return CInnerConverter::customConvert(enc, WCHAR_CHARSET, stringSrc, wStringDst);
}

bool CCharsetConverter::utf8ToStringCharset(const std::string& utf8StringSrc, std::string& stringDst)
{
  return CInnerConverter::stdConvert(Utf8ToUserCharset, utf8StringSrc, stringDst);
}

bool CCharsetConverter::utf8ToStringCharset(std::string& stringSrcDst)
{
  std::string strSrc(stringSrcDst);
  return utf8ToStringCharset(strSrc, stringSrcDst);
}

bool CCharsetConverter::ToUtf8(const std::string& strSourceCharset, const std::string& stringSrc, std::string& utf8StringDst)
{
  if (strSourceCharset == "UTF-8")
  { // simple case - no conversion necessary
    utf8StringDst = stringSrc;
    return true;
  }
  
  return CInnerConverter::customConvert(strSourceCharset, "UTF-8", stringSrc, utf8StringDst);
}

bool CCharsetConverter::utf8To(const std::string& strDestCharset, const std::string& utf8StringSrc, std::string& stringDst)
{
  if (strDestCharset == "UTF-8")
  { // simple case - no conversion necessary
    stringDst = utf8StringSrc;
    return true;
  }

  return CInnerConverter::customConvert(UTF8_SOURCE, strDestCharset, utf8StringSrc, stringDst);
}

bool CCharsetConverter::utf8To(const std::string& strDestCharset, const std::string& utf8StringSrc, std::u16string& utf16StringDst)
{
  return CInnerConverter::customConvert(UTF8_SOURCE, strDestCharset, utf8StringSrc, utf16StringDst);
}

bool CCharsetConverter::utf8To(const std::string& strDestCharset, const std::string& utf8StringSrc, std::u32string& utf32StringDst)
{
  return CInnerConverter::customConvert(UTF8_SOURCE, strDestCharset, utf8StringSrc, utf32StringDst);
}

bool CCharsetConverter::unknownToUTF8(std::string& stringSrcDst)
{
  std::string source(stringSrcDst);
  return unknownToUTF8(source, stringSrcDst);
}

bool CCharsetConverter::unknownToUTF8(const std::string& stringSrc, std::string& utf8StringDst, bool failOnBadChar /*= false*/)
{
  // checks whether it's utf8 already, and if not converts using the sourceCharset if given, else the string charset
  if (isValidUtf8(stringSrc))
  {
    utf8StringDst = stringSrc;
    return true;
  }
  return CInnerConverter::stdConvert(UserCharsetToUtf8, stringSrc, utf8StringDst, failOnBadChar);
}

bool CCharsetConverter::wToUTF8(const std::wstring& wStringSrc, std::string& utf8StringDst, bool failOnBadChar /*= false*/)
{
  return CInnerConverter::stdConvert(WtoUtf8, wStringSrc, utf8StringDst, failOnBadChar);
}

bool CCharsetConverter::utf16BEtoUTF8(const std::u16string& utf16StringSrc, std::string& utf8StringDst)
{
  return CInnerConverter::stdConvert(Utf16BEtoUtf8, utf16StringSrc, utf8StringDst);
}

bool CCharsetConverter::utf16LEtoUTF8(const std::u16string& utf16StringSrc,
                                      std::string& utf8StringDst)
{
  return CInnerConverter::stdConvert(Utf16LEtoUtf8, utf16StringSrc, utf8StringDst);
}

bool CCharsetConverter::ucs2ToUTF8(const std::u16string& ucs2StringSrc, std::string& utf8StringDst)
{
  return CInnerConverter::stdConvert(Ucs2CharsetToUtf8, ucs2StringSrc,utf8StringDst);
}

bool CCharsetConverter::utf16LEtoW(const std::u16string& utf16String, std::wstring& wString)
{
  return CInnerConverter::stdConvert(Utf16LEtoW, utf16String, wString);
}

bool CCharsetConverter::utf32ToStringCharset(const std::u32string& utf32StringSrc, std::string& stringDst)
{
  return CInnerConverter::stdConvert(Utf32ToUserCharset, utf32StringSrc, stringDst);
}

bool CCharsetConverter::utf8ToSystem(std::string& stringSrcDst, bool failOnBadChar /*= false*/)
{
  std::string strSrc(stringSrcDst);
  return CInnerConverter::stdConvert(Utf8ToSystem, strSrc, stringSrcDst, failOnBadChar);
}

// Taken from RFC2640
bool CCharsetConverter::isValidUtf8(const char* buf, unsigned int len)
{
  const unsigned char* endbuf = (unsigned char*)buf + len;
  unsigned char byte2mask=0x00, c;
  int trailing=0; // trailing (continuation) bytes to follow

  while ((unsigned char*)buf != endbuf)
  {
    c = *buf++;
    if (trailing)
      if ((c & 0xc0) == 0x80) // does trailing byte follow UTF-8 format ?
      {
        if (byte2mask) // need to check 2nd byte for proper range
        {
          if (c & byte2mask) // are appropriate bits set ?
            byte2mask = 0x00;
          else
            return false;
        }
        trailing--;
      }
      else
        return 0;
    else
      if ((c & 0x80) == 0x00) continue; // valid 1-byte UTF-8
      else if ((c & 0xe0) == 0xc0)      // valid 2-byte UTF-8
        if (c & 0x1e)                   //is UTF-8 byte in proper range ?
          trailing = 1;
        else
          return false;
      else if ((c & 0xf0) == 0xe0)      // valid 3-byte UTF-8
       {
        if (!(c & 0x0f))                // is UTF-8 byte in proper range ?
          byte2mask = 0x20;             // if not set mask
        trailing = 2;                   // to check next byte
      }
      else if ((c & 0xf8) == 0xf0)      // valid 4-byte UTF-8
      {
        if (!(c & 0x07))                // is UTF-8 byte in proper range ?
          byte2mask = 0x30;             // if not set mask
        trailing = 3;                   // to check next byte
      }
      else if ((c & 0xfc) == 0xf8)      // valid 5-byte UTF-8
      {
        if (!(c & 0x03))                // is UTF-8 byte in proper range ?
          byte2mask = 0x38;             // if not set mask
        trailing = 4;                   // to check next byte
      }
      else if ((c & 0xfe) == 0xfc)      // valid 6-byte UTF-8
      {
        if (!(c & 0x01))                // is UTF-8 byte in proper range ?
          byte2mask = 0x3c;             // if not set mask
        trailing = 5;                   // to check next byte
      }
      else
        return false;
  }
  return trailing == 0;
}

bool CCharsetConverter::isValidUtf8(const std::string& str)
{
  return isValidUtf8(str.c_str(), str.size());
}

bool CCharsetConverter::utf8logicalToVisualBiDi(const std::string& utf8StringSrc, std::string& utf8StringDst)
{
  return CInnerConverter::logicalToVisualBiDi(utf8StringSrc, utf8StringDst, FRIBIDI_UTF8, FRIBIDI_TYPE_RTL);
}

void CCharsetConverter::SettingOptionsCharsetsFiller(const CSetting* setting, std::vector< std::pair<std::string, std::string> >& list, std::string& current)
{
  vector<std::string> vecCharsets = g_charsetConverter.getCharsetLabels();
  sort(vecCharsets.begin(), vecCharsets.end(), sortstringbyname());

  list.push_back(make_pair(g_localizeStrings.Get(13278), "DEFAULT")); // "Default"
  for (int i = 0; i < (int) vecCharsets.size(); ++i)
    list.push_back(make_pair(vecCharsets[i], g_charsetConverter.getCharsetNameByLabel(vecCharsets[i])));
}
