/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCOMPtr.h"
#include "nsQueryObject.h"
#include "nsIServiceManager.h"
#include "nsPrintOptionsX.h"
#include "nsPrintSettingsX.h"

using namespace mozilla::embedding;

nsPrintOptionsX::nsPrintOptionsX()
{
}

nsPrintOptionsX::~nsPrintOptionsX()
{
}

NS_IMETHODIMP
nsPrintOptionsX::SerializeToPrintData(nsIPrintSettings* aSettings,
                                      nsIWebBrowserPrint* aWBP,
                                      PrintData* data)
{
  nsresult rv = nsPrintOptions::SerializeToPrintData(aSettings, aWBP, data);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (aWBP) {
    // When serializing an nsIWebBrowserPrint, we need to pass up the first
    // document name. We could pass up the entire collection of document
    // names, but the OS X printing prompt code only really cares about
    // the first one, so we just send the first to save IPC traffic.
    char16_t** docTitles;
    uint32_t titleCount;
    rv = aWBP->EnumerateDocumentNames(&titleCount, &docTitles);
    if (NS_SUCCEEDED(rv) && titleCount > 0) {
      data->printJobName().Assign(docTitles[0]);
    }

    for (int32_t i = titleCount - 1; i >= 0; i--) {
      free(docTitles[i]);
    }
    free(docTitles);
    docTitles = nullptr;
  }

  RefPtr<nsPrintSettingsX> settingsX(do_QueryObject(aSettings));
  if (NS_WARN_IF(!settingsX)) {
    return NS_ERROR_FAILURE;
  }

  NSPrintInfo* printInfo = settingsX->GetCocoaPrintInfo();
  if (NS_WARN_IF(!printInfo)) {
    return NS_ERROR_FAILURE;
  }

  NSDictionary* dict = [printInfo dictionary];
  if (NS_WARN_IF(!dict)) {
    return NS_ERROR_FAILURE;
  }

  NSString* printerName = [dict objectForKey: NSPrintPrinterName];
  if (printerName) {
    nsCocoaUtils::GetStringForNSString(printerName, data->printerName());
  }

  NSString* faxNumber = [dict objectForKey: NSPrintFaxNumber];
  if (faxNumber) {
    nsCocoaUtils::GetStringForNSString(faxNumber, data->faxNumber());
  }

  NSURL* printToFileURL = [dict objectForKey: NSPrintJobSavingURL];
  if (printToFileURL) {
    nsCocoaUtils::GetStringForNSString([printToFileURL absoluteString],
                                       data->toFileName());
  }

  NSDate* printTime = [dict objectForKey: NSPrintTime];
  if (printTime) {
    NSTimeInterval timestamp = [printTime timeIntervalSinceReferenceDate];
    data->printTime() = timestamp;
  }

  NSString* disposition = [dict objectForKey: NSPrintJobDisposition];
  if (disposition) {
    nsCocoaUtils::GetStringForNSString(disposition, data->disposition());
  }

  data->numCopies() = [[dict objectForKey: NSPrintCopies] intValue];
  data->printAllPages() = [[dict objectForKey: NSPrintAllPages] boolValue];
  data->startPageRange() = [[dict objectForKey: NSPrintFirstPage] intValue];
  data->endPageRange() = [[dict objectForKey: NSPrintLastPage] intValue];
  data->mustCollate() = [[dict objectForKey: NSPrintMustCollate] boolValue];
  data->printReversed() = [[dict objectForKey: NSPrintReversePageOrder] boolValue];
  data->pagesAcross() = [[dict objectForKey: NSPrintPagesAcross] unsignedShortValue];
  data->pagesDown() = [[dict objectForKey: NSPrintPagesDown] unsignedShortValue];
  data->detailedErrorReporting() = [[dict objectForKey: NSPrintDetailedErrorReporting] boolValue];
  data->addHeaderAndFooter() = [[dict objectForKey: NSPrintHeaderAndFooter] boolValue];
  data->fileNameExtensionHidden() =
    [[dict objectForKey: NSPrintJobSavingFileNameExtensionHidden] boolValue];

  bool printSelectionOnly = [[dict objectForKey: NSPrintSelectionOnly] boolValue];
  aSettings->SetPrintOptions(nsIPrintSettings::kEnableSelectionRB,
                             printSelectionOnly);
  aSettings->GetPrintOptionsBits(&data->optionFlags());

  return NS_OK;
}

NS_IMETHODIMP
nsPrintOptionsX::DeserializeToPrintSettings(const PrintData& data,
                                            nsIPrintSettings* settings)
{
  nsresult rv = nsPrintOptions::DeserializeToPrintSettings(data, settings);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  RefPtr<nsPrintSettingsX> settingsX(do_QueryObject(settings));
  if (NS_WARN_IF(!settingsX)) {
    return NS_ERROR_FAILURE;
  }

  NSPrintInfo* sharedPrintInfo = [NSPrintInfo sharedPrintInfo];
  if (NS_WARN_IF(!sharedPrintInfo)) {
    return NS_ERROR_FAILURE;
  }

  NSDictionary* sharedDict = [sharedPrintInfo dictionary];
  if (NS_WARN_IF(!sharedDict)) {
    return NS_ERROR_FAILURE;
  }

  // We need to create a new NSMutableDictionary to pass to NSPrintInfo with
  // the values that we got from the other process.
  NSMutableDictionary* newPrintInfoDict =
    [NSMutableDictionary dictionaryWithDictionary:sharedDict];
  if (NS_WARN_IF(!newPrintInfoDict)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  NSString* printerName = nsCocoaUtils::ToNSString(data.printerName());
  if (printerName) {
    NSPrinter* printer = [NSPrinter printerWithName: printerName];
    if (NS_WARN_IF(!printer)) {
      return NS_ERROR_FAILURE;
    }
    [newPrintInfoDict setObject: printer forKey: NSPrintPrinter];
    [newPrintInfoDict setObject: printerName forKey: NSPrintPrinterName];
  }

  [newPrintInfoDict setObject: [NSNumber numberWithInt: data.numCopies()]
                    forKey: NSPrintCopies];
  [newPrintInfoDict setObject: [NSNumber numberWithBool: data.printAllPages()]
                    forKey: NSPrintAllPages];
  [newPrintInfoDict setObject: [NSNumber numberWithInt: data.startPageRange()]
                    forKey: NSPrintFirstPage];
  [newPrintInfoDict setObject: [NSNumber numberWithInt: data.endPageRange()]
                    forKey: NSPrintLastPage];
  [newPrintInfoDict setObject: [NSNumber numberWithBool: data.mustCollate()]
                    forKey: NSPrintMustCollate];
  [newPrintInfoDict setObject: [NSNumber numberWithBool: data.printReversed()]
                    forKey: NSPrintReversePageOrder];

  [newPrintInfoDict setObject: nsCocoaUtils::ToNSString(data.disposition())
                    forKey: NSPrintJobDisposition];

  [newPrintInfoDict setObject: [NSNumber numberWithShort: data.pagesAcross()]
                    forKey: NSPrintPagesAcross];
  [newPrintInfoDict setObject: [NSNumber numberWithShort: data.pagesDown()]
                    forKey: NSPrintPagesDown];
  [newPrintInfoDict setObject: [NSNumber numberWithBool: data.detailedErrorReporting()]
                    forKey: NSPrintDetailedErrorReporting];
  [newPrintInfoDict setObject: nsCocoaUtils::ToNSString(data.faxNumber())
                    forKey: NSPrintFaxNumber];
  [newPrintInfoDict setObject: [NSNumber numberWithBool: data.addHeaderAndFooter()]
                    forKey: NSPrintHeaderAndFooter];
  [newPrintInfoDict setObject: [NSNumber numberWithBool: data.fileNameExtensionHidden()]
                    forKey: NSPrintJobSavingFileNameExtensionHidden];

  // At this point, the base class should have properly deserialized the print
  // options bitfield for nsIPrintSettings, so that it holds the correct value
  // for kEnableSelectionRB, which we use to set NSPrintSelectionOnly.

  bool printSelectionOnly = false;
  rv = settings->GetPrintOptions(nsIPrintSettings::kEnableSelectionRB, &printSelectionOnly);
  if (NS_SUCCEEDED(rv)) {
    [newPrintInfoDict setObject: [NSNumber numberWithBool: printSelectionOnly]
                      forKey: NSPrintSelectionOnly];
  } else {
    [newPrintInfoDict setObject: [NSNumber numberWithBool: NO]
                      forKey: NSPrintSelectionOnly];
  }

  NSURL* jobSavingURL =
    [NSURL URLWithString:[nsCocoaUtils::ToNSString(data.toFileName())
                          stringByAddingPercentEscapesUsingEncoding: NSUTF8StringEncoding]];
  if (jobSavingURL) {
    [newPrintInfoDict setObject: jobSavingURL forKey: NSPrintJobSavingURL];
  }

  NSTimeInterval timestamp = data.printTime();
  NSDate* printTime = [NSDate dateWithTimeIntervalSinceReferenceDate: timestamp];
  if (printTime) {
    [newPrintInfoDict setObject: printTime forKey: NSPrintTime];
  }

  // Next, we create a new NSPrintInfo with the values in our dictionary.
  NSPrintInfo* newPrintInfo =
    [[NSPrintInfo alloc] initWithDictionary: newPrintInfoDict];
  if (NS_WARN_IF(!newPrintInfo)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // And now swap in the new NSPrintInfo we've just populated.
  settingsX->SetCocoaPrintInfo(newPrintInfo);
  [newPrintInfo release];

  return NS_OK;
}

nsresult
nsPrintOptionsX::ReadPrefs(nsIPrintSettings* aPS, const nsAString& aPrinterName, uint32_t aFlags)
{
  nsresult rv;
  
  rv = nsPrintOptions::ReadPrefs(aPS, aPrinterName, aFlags);
  NS_ASSERTION(NS_SUCCEEDED(rv), "nsPrintOptions::ReadPrefs() failed");
  
  RefPtr<nsPrintSettingsX> printSettingsX(do_QueryObject(aPS));
  if (!printSettingsX)
    return NS_ERROR_NO_INTERFACE;
  rv = printSettingsX->ReadPageFormatFromPrefs();
  
  return NS_OK;
}

nsresult nsPrintOptionsX::_CreatePrintSettings(nsIPrintSettings **_retval)
{
  nsresult rv;
  *_retval = nullptr;

  nsPrintSettingsX* printSettings = new nsPrintSettingsX; // does not initially ref count
  NS_ENSURE_TRUE(printSettings, NS_ERROR_OUT_OF_MEMORY);
  NS_ADDREF(*_retval = printSettings);

  rv = printSettings->Init();
  if (NS_FAILED(rv)) {
    NS_RELEASE(*_retval);
    return rv;
  }

  InitPrintSettingsFromPrefs(*_retval, false, nsIPrintSettings::kInitSaveAll);
  return rv;
}

nsresult
nsPrintOptionsX::WritePrefs(nsIPrintSettings* aPS, const nsAString& aPrinterName, uint32_t aFlags)
{
  nsresult rv;

  rv = nsPrintOptions::WritePrefs(aPS, aPrinterName, aFlags);
  NS_ASSERTION(NS_SUCCEEDED(rv), "nsPrintOptions::WritePrefs() failed");

  RefPtr<nsPrintSettingsX> printSettingsX(do_QueryObject(aPS));
  if (!printSettingsX)
    return NS_ERROR_NO_INTERFACE;
  rv = printSettingsX->WritePageFormatToPrefs();
  NS_ASSERTION(NS_SUCCEEDED(rv), "nsPrintSettingsX::WritePageFormatToPrefs() failed");

  return NS_OK;
}
