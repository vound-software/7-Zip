// Zip/OutHandler.cpp

#include "StdAfx.h"

// #include "../../Handler/FileTimeType.h"
#include "Handler.h"
#include "Archive/Zip/OutEngine.h"
#include "Archive/Common/ItemNameUtils.h"
#include "Common/StringConvert.h"
#include "UpdateMain.h"

#include "Windows/PropVariant.h"
#include "Windows/Time.h"
#include "Windows/COMTry.h"

#include "../Common/FormatCryptoInterface.h"

using namespace NArchive;
using namespace NZip;

using namespace NWindows;
using namespace NCOM;
using namespace NTime;

STDMETHODIMP CZipHandler::GetFileTimeType(UINT32 *aType)
{
  *aType = NFileTimeType::kDOS;
  return S_OK;
}

STDMETHODIMP CZipHandler::DeleteItems(IOutStream *anOutStream, 
    const UINT32* anIndexes, UINT32 aNumItems, IUpdateCallBack *anUpdateCallBack)
{
  COM_TRY_BEGIN
  CRecordVector<bool> aCompressStatuses;
  CRecordVector<UINT32> aCopyIndexes;
  int anIndex = 0;
  for(int i = 0; i < m_Items.Size(); i++)
  {
    if(anIndex < aNumItems && i == anIndexes[anIndex])
      anIndex++;
    else
    {
      aCompressStatuses.Add(false);
      aCopyIndexes.Add(i);
    }
  }
  UpdateMain(m_Items, aCompressStatuses,
      CObjectVector<CUpdateItemInfo>(), aCopyIndexes, 
      anOutStream, m_ArchiveIsOpen ? &m_Archive : NULL, NULL, anUpdateCallBack);
  return S_OK;
  COM_TRY_END
}

STDMETHODIMP CZipHandler::UpdateItems(IOutStream *anOutStream, UINT32 aNumItems,
    IUpdateCallBack *anUpdateCallBack)
{
  COM_TRY_BEGIN
  CRecordVector<bool> aCompressStatuses;
  CObjectVector<CUpdateItemInfo> anUpdateItems;
  CRecordVector<UINT32> aCopyIndexes;
  int anIndex = 0;
  for(int i = 0; i < aNumItems; i++)
  {
    CUpdateItemInfo anUpdateItemInfo;
    INT32 anCompress;
    INT32 anExistInArchive;
    INT32 anIndexInServer;
    FILETIME anUTCFileTime;
    UINT64 aSize;
    CComBSTR aName;
    HRESULT aResult = anUpdateCallBack->GetUpdateItemInfo(i,
        &anCompress, // 1 - compress 0 - copy
        &anExistInArchive,
        &anIndexInServer,
        &anUpdateItemInfo.Attributes,
        NULL,
        NULL,
        &anUTCFileTime,
        &aSize, 
        &aName);
    if (aResult != S_OK)
      return aResult;
    if (MyBoolToBool(anCompress))
    {
      FILETIME aLocalFileTime;
      if(!FileTimeToLocalFileTime(&anUTCFileTime, &aLocalFileTime))
        return E_FAIL;
      if(!FileTimeToDosTime(aLocalFileTime, anUpdateItemInfo.Time))
        return E_FAIL;
      if(aSize > _UI32_MAX)
        return E_FAIL;
      anUpdateItemInfo.Size = aSize;
      anUpdateItemInfo.Name = UnicodeStringToMultiByte(
          NItemName::MakeLegalName((BSTR)aName), CP_OEMCP);
      if (anUpdateItemInfo.IsDirectory())
        anUpdateItemInfo.Name += '/';
      anUpdateItemInfo.IndexInClient = i;
      if(MyBoolToBool(anExistInArchive))
      {
        const NArchive::NZip::CItemInfoEx &anItemInfo = m_Items[anIndexInServer];
        anUpdateItemInfo.Commented = anItemInfo.IsCommented();
        if(anUpdateItemInfo.Commented)
        {
          anUpdateItemInfo.CommentRange.Position = anItemInfo.GetCommentPosition();
          anUpdateItemInfo.CommentRange.Size  = anItemInfo.CommentSize;
        }
      }
      else
        anUpdateItemInfo.Commented = false;
      aCompressStatuses.Add(true);
      anUpdateItems.Add(anUpdateItemInfo);
    }
    else
    {
      aCompressStatuses.Add(false);
      aCopyIndexes.Add(anIndexInServer);
    }
  }
  
  CComPtr<ICryptoGetTextPassword2> getTextPassword;
  if (!getTextPassword)
  {
    CComPtr<IUpdateCallBack> udateCallBack2(anUpdateCallBack);
    udateCallBack2.QueryInterface(&getTextPassword);
  }
  
  if (getTextPassword)
  {
    CComBSTR password;
    INT32 passwordIsDefined;
    RETURN_IF_NOT_S_OK(getTextPassword->CryptoGetTextPassword2(
        &passwordIsDefined, &password));
    if (m_Method.PasswordIsDefined = IntToBool(passwordIsDefined))
      m_Method.Password = UnicodeStringToMultiByte(
          (const wchar_t *)password, CP_OEMCP);
  }
  else
  {
    m_Method.PasswordIsDefined = false;
  }

  return UpdateMain(m_Items, aCompressStatuses,
      anUpdateItems, aCopyIndexes, anOutStream, m_ArchiveIsOpen ? &m_Archive : NULL, 
      &m_Method, anUpdateCallBack);
  COM_TRY_END
}

static const UINT32 kNumPassesNormal = 1;
static const UINT32 kNumPassesMX  = 3;

static const UINT32 kMatchFastLenNormal  = 32;
static const UINT32 kMatchFastLenMX  = 64;


STDMETHODIMP CZipHandler::SetProperties(const BSTR *aNames, const PROPVARIANT *aValues, INT32 aNumProperties)
{
  InitMethodProperties();
  bool aM0 = false;
  for (int i = 0; i < aNumProperties; i++)
  {
    UString aString = UString(aNames[i]);
    aString.MakeUpper();
    const PROPVARIANT &aValue = aValues[i];
    if (aString == L"X")
    {
      m_Method.NumPasses = kNumPassesMX;
      m_Method.NumFastBytes = kMatchFastLenMX;
      aM0 = false;
    }
    else if (aString == L"0")
      aM0 = true;
    else if (aString == L"1")
    {
      aM0 = false;
      InitMethodProperties();
    }
    else if (aString == L"PASS")
    {
      if (aValue.vt != VT_UI4)
        return E_INVALIDARG;
      m_Method.NumPasses = aValue.ulVal;
      if (m_Method.NumPasses < 1 || m_Method.NumPasses > 4)
        return E_INVALIDARG;
    }
    else if (aString == L"FB")
    {
      if (aValue.vt != VT_UI4)
        return E_INVALIDARG;
      m_Method.NumFastBytes = aValue.ulVal;
      if (m_Method.NumFastBytes < 3 || m_Method.NumFastBytes > 255)
        return E_INVALIDARG;
    }
    else
      return E_INVALIDARG;
  }
  m_Method.MethodSequence.Clear();
  if (!aM0)
    m_Method.MethodSequence.Add(NFileHeader::NCompressionMethod::kDeflated);
  m_Method.MethodSequence.Add(NFileHeader::NCompressionMethod::kStored);
  return S_OK;
}  
