/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for all our document implementations.
 */

#include "nsDocument.h"
#include "nsIDocumentInlines.h"
#include "mozilla/AnimationComparator.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/BinarySearch.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/EffectSet.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Likely.h"
#include <algorithm>

#include "mozilla/Logging.h"
#include "plstr.h"
#include "mozilla/Snprintf.h"

#include "mozilla/Telemetry.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsILoadContext.h"
#include "nsITextControlFrame.h"
#include "nsUnicharUtils.h"
#include "nsContentList.h"
#include "nsCSSPseudoElements.h"
#include "nsIObserver.h"
#include "nsIBaseWindow.h"
#include "mozilla/css/Loader.h"
#include "mozilla/css/ImageLoader.h"
#include "nsDocShell.h"
#include "nsDocShellLoadTypes.h"
#include "nsIDocShellTreeItem.h"
#include "nsCOMArray.h"
#include "nsQueryObject.h"
#include "nsDOMClassInfo.h"
#include "mozilla/Services.h"
#include "nsScreen.h"

#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/EventStateManager.h"
#include "nsIDOMNodeFilter.h"

#include "nsIDOMStyleSheet.h"
#include "mozilla/dom/Attr.h"
#include "nsIDOMDOMImplementation.h"
#include "nsIDOMDocumentXBL.h"
#include "mozilla/dom/Element.h"
#include "nsGenericHTMLElement.h"
#include "mozilla/dom/CDATASection.h"
#include "mozilla/dom/ProcessingInstruction.h"
#include "nsDOMString.h"
#include "nsNodeUtils.h"
#include "nsLayoutUtils.h" // for GetFrameForPoint
#include "nsIFrame.h"
#include "nsITabChild.h"

#include "nsRange.h"
#include "nsIDOMText.h"
#include "nsIDOMComment.h"
#include "mozilla/dom/DocumentType.h"
#include "mozilla/dom/NodeIterator.h"
#include "mozilla/dom/TreeWalker.h"

#include "nsIServiceManager.h"
#include "mozilla/dom/workers/ServiceWorkerManager.h"
#include "imgLoader.h"

#include "nsCanvasFrame.h"
#include "nsContentCID.h"
#include "nsError.h"
#include "nsPresShell.h"
#include "nsPresContext.h"
#include "nsIJSON.h"
#include "nsThreadUtils.h"
#include "nsNodeInfoManager.h"
#include "nsIFileChannel.h"
#include "nsIMultiPartChannel.h"
#include "nsIRefreshURI.h"
#include "nsIWebNavigation.h"
#include "nsIScriptError.h"
#include "nsISimpleEnumerator.h"
#include "nsStyleSheetService.h"

#include "nsNetUtil.h"     // for NS_NewURI
#include "nsIInputStreamChannel.h"
#include "nsIAuthPrompt.h"
#include "nsIAuthPrompt2.h"

#include "nsIScriptSecurityManager.h"
#include "nsIPermissionManager.h"
#include "nsIPrincipal.h"

#include "nsIDOMWindow.h"
#include "nsPIDOMWindow.h"
#include "nsIDOMElement.h"
#include "nsFocusManager.h"

// for radio group stuff
#include "nsIDOMHTMLInputElement.h"
#include "nsIRadioVisitor.h"
#include "nsIFormControl.h"

#include "nsBidiUtils.h"

#include "nsIParserService.h"
#include "nsContentCreatorFunctions.h"

#include "nsIScriptContext.h"
#include "nsBindingManager.h"
#include "nsIDOMHTMLDocument.h"
#include "nsHTMLDocument.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsIRequest.h"
#include "nsHostObjectProtocolHandler.h"

#include "nsCharsetSource.h"
#include "nsIParser.h"
#include "nsIContentSink.h"

#include "nsDateTimeFormatCID.h"
#include "nsIDateTimeFormat.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventStates.h"
#include "mozilla/InternalMutationEvent.h"
#include "nsDOMCID.h"

#include "jsapi.h"
#include "nsIXPConnect.h"
#include "nsCCUncollectableMarker.h"
#include "nsIContentPolicy.h"
#include "nsContentPolicyUtils.h"
#include "nsICategoryManager.h"
#include "nsIDocumentLoaderFactory.h"
#include "nsIDocumentLoader.h"
#include "nsIContentViewer.h"
#include "nsIXMLContentSink.h"
#include "nsIXULDocument.h"
#include "nsIPrompt.h"
#include "nsIPropertyBag2.h"
#include "mozilla/dom/PageTransitionEvent.h"
#include "mozilla/dom/StyleRuleChangeEvent.h"
#include "mozilla/dom/StyleSheetChangeEvent.h"
#include "mozilla/dom/StyleSheetApplicableStateChangeEvent.h"
#include "nsJSUtils.h"
#include "nsFrameLoader.h"
#include "nsEscape.h"
#include "nsObjectLoadingContent.h"
#include "nsHtml5TreeOpExecutor.h"
#include "mozilla/dom/HTMLLinkElement.h"
#include "mozilla/dom/HTMLMediaElement.h"
#include "mozilla/dom/HTMLIFrameElement.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/MediaSource.h"

#include "mozAutoDocUpdate.h"
#include "nsGlobalWindow.h"
#include "mozilla/dom/EncodingUtils.h"
#include "nsDOMNavigationTiming.h"

#include "nsSMILAnimationController.h"
#include "imgIContainer.h"
#include "nsSVGUtils.h"
#include "SVGElementFactory.h"

#include "nsRefreshDriver.h"

// FOR CSP (autogenerated by xpidl)
#include "nsIContentSecurityPolicy.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/dom/nsCSPService.h"
#include "nsHTMLStyleSheet.h"
#include "nsHTMLCSSStyleSheet.h"
#include "SVGAttrAnimationRuleProcessor.h"
#include "mozilla/dom/DOMImplementation.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/Comment.h"
#include "nsTextNode.h"
#include "mozilla/dom/Link.h"
#include "mozilla/dom/HTMLElementBinding.h"
#include "mozilla/dom/SVGElementBinding.h"
#include "nsXULAppAPI.h"
#include "mozilla/dom/Touch.h"
#include "mozilla/dom/TouchEvent.h"

#include "mozilla/Preferences.h"

#include "imgILoader.h"
#include "imgRequestProxy.h"
#include "nsWrapperCacheInlines.h"
#include "nsSandboxFlags.h"
#include "nsIAddonPolicyService.h"
#include "nsIAppsService.h"
#include "mozilla/dom/AnimatableBinding.h"
#include "mozilla/dom/AnonymousContent.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/MediaQueryList.h"
#include "mozilla/dom/NodeFilterBinding.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/dom/TabChild.h"
#include "mozilla/dom/UndoManager.h"
#include "mozilla/dom/WebComponentsBinding.h"
#include "nsFrame.h"
#include "nsDOMCaretPosition.h"
#include "nsIDOMHTMLTextAreaElement.h"
#include "nsViewportInfo.h"
#include "nsIContentPermissionPrompt.h"
#include "mozilla/StaticPtr.h"
#include "nsITextControlElement.h"
#include "nsIDOMNSEditableElement.h"
#include "nsIEditor.h"
#include "nsIDOMCSSStyleRule.h"
#include "mozilla/css/Rule.h"
#include "nsIDOMLocation.h"
#include "nsIHttpChannelInternal.h"
#include "nsISecurityConsoleMessage.h"
#include "nsCharSeparatedTokenizer.h"
#include "mozilla/dom/XPathEvaluator.h"
#include "mozilla/dom/XPathNSResolverBinding.h"
#include "mozilla/dom/XPathResult.h"
#include "nsIDocumentEncoder.h"
#include "nsIDocumentActivity.h"
#include "nsIStructuredCloneContainer.h"
#include "nsIMutableArray.h"
#include "nsContentPermissionHelper.h"
#include "mozilla/dom/DOMStringList.h"
#include "nsWindowMemoryReporter.h"
#include "nsLocation.h"
#include "mozilla/dom/FontFaceSet.h"
#include "mozilla/dom/BoxObject.h"
#include "gfxVR.h"
#include "gfxPrefs.h"
#include "nsISupportsPrimitives.h"
#include "mozilla/StyleSetHandle.h"
#include "mozilla/StyleSetHandleInlines.h"
#include "mozilla/StyleSheetHandle.h"
#include "mozilla/StyleSheetHandleInlines.h"

#include "nsISpeculativeConnect.h"

#include "mozilla/MediaManager.h"
#ifdef MOZ_WEBRTC
#include "IPeerConnection.h"
#endif // MOZ_WEBRTC

using namespace mozilla;
using namespace mozilla::dom;

typedef nsTArray<Link*> LinkArray;

static LazyLogModule gDocumentLeakPRLog("DocumentLeak");
static LazyLogModule gCspPRLog("CSP");

#define NAME_NOT_VALID ((nsSimpleContentList*)1)

nsIdentifierMapEntry::~nsIdentifierMapEntry()
{
}

void
nsIdentifierMapEntry::Traverse(nsCycleCollectionTraversalCallback* aCallback)
{
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback,
                                     "mIdentifierMap mNameContentList");
  aCallback->NoteXPCOMChild(static_cast<nsIDOMNodeList*>(mNameContentList));

  if (mImageElement) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback,
                                       "mIdentifierMap mImageElement element");
    nsIContent* imageElement = mImageElement;
    aCallback->NoteXPCOMChild(imageElement);
  }
}

bool
nsIdentifierMapEntry::IsEmpty()
{
  return mIdContentList.IsEmpty() && !mNameContentList &&
         !mChangeCallbacks && !mImageElement;
}

Element*
nsIdentifierMapEntry::GetIdElement()
{
  return mIdContentList.SafeElementAt(0);
}

Element*
nsIdentifierMapEntry::GetImageIdElement()
{
  return mImageElement ? mImageElement.get() : GetIdElement();
}

void
nsIdentifierMapEntry::AppendAllIdContent(nsCOMArray<nsIContent>* aElements)
{
  for (size_t i = 0; i < mIdContentList.Length(); ++i) {
    aElements->AppendObject(mIdContentList[i]);
  }
}

void
nsIdentifierMapEntry::AddContentChangeCallback(nsIDocument::IDTargetObserver aCallback,
                                               void* aData, bool aForImage)
{
  if (!mChangeCallbacks) {
    mChangeCallbacks = new nsTHashtable<ChangeCallbackEntry>;
  }

  ChangeCallback cc = { aCallback, aData, aForImage };
  mChangeCallbacks->PutEntry(cc);
}

void
nsIdentifierMapEntry::RemoveContentChangeCallback(nsIDocument::IDTargetObserver aCallback,
                                                  void* aData, bool aForImage)
{
  if (!mChangeCallbacks)
    return;
  ChangeCallback cc = { aCallback, aData, aForImage };
  mChangeCallbacks->RemoveEntry(cc);
  if (mChangeCallbacks->Count() == 0) {
    mChangeCallbacks = nullptr;
  }
}

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_CLASS(Registry)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(Registry)
  for (auto iter = tmp->mCustomDefinitions.Iter(); !iter.Done(); iter.Next()) {
    aCallbacks.Trace(&iter.UserData()->mPrototype,
                     "mCustomDefinitions prototype",
                     aClosure);
  }
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(Registry)
  for (auto iter = tmp->mCustomDefinitions.Iter(); !iter.Done(); iter.Next()) {
    nsAutoPtr<LifecycleCallbacks>& callbacks = iter.UserData()->mCallbacks;

    if (callbacks->mAttributeChangedCallback.WasPassed()) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb,
        "mCustomDefinitions->mCallbacks->mAttributeChangedCallback");
      cb.NoteXPCOMChild(callbacks->mAttributeChangedCallback.Value());
    }

    if (callbacks->mCreatedCallback.WasPassed()) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb,
        "mCustomDefinitions->mCallbacks->mCreatedCallback");
      cb.NoteXPCOMChild(callbacks->mCreatedCallback.Value());
    }

    if (callbacks->mAttachedCallback.WasPassed()) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb,
        "mCustomDefinitions->mCallbacks->mAttachedCallback");
      cb.NoteXPCOMChild(callbacks->mAttachedCallback.Value());
    }

    if (callbacks->mDetachedCallback.WasPassed()) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb,
        "mCustomDefinitions->mCallbacks->mDetachedCallback");
      cb.NoteXPCOMChild(callbacks->mDetachedCallback.Value());
    }
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Registry)
  tmp->mCustomDefinitions.Clear();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Registry)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(Registry)
NS_IMPL_CYCLE_COLLECTING_RELEASE(Registry)

Registry::Registry()
{
  mozilla::HoldJSObjects(this);
}

Registry::~Registry()
{
  mozilla::DropJSObjects(this);
}

void
CustomElementCallback::Call()
{
  ErrorResult rv;
  switch (mType) {
    case nsIDocument::eCreated:
    {
      // For the duration of this callback invocation, the element is being created
      // flag must be set to true.
      mOwnerData->mElementIsBeingCreated = true;

      // The callback hasn't actually been invoked yet, but we need to flip
      // this now in order to enqueue the attached callback. This is a spec
      // bug (w3c bug 27437).
      mOwnerData->mCreatedCallbackInvoked = true;

      // If ELEMENT is in a document and this document has a browsing context,
      // enqueue attached callback for ELEMENT.
      nsIDocument* document = mThisObject->GetComposedDoc();
      if (document && document->GetDocShell()) {
        document->EnqueueLifecycleCallback(nsIDocument::eAttached, mThisObject);
      }

      static_cast<LifecycleCreatedCallback *>(mCallback.get())->Call(mThisObject, rv);
      mOwnerData->mElementIsBeingCreated = false;
      break;
    }
    case nsIDocument::eAttached:
      static_cast<LifecycleAttachedCallback *>(mCallback.get())->Call(mThisObject, rv);
      break;
    case nsIDocument::eDetached:
      static_cast<LifecycleDetachedCallback *>(mCallback.get())->Call(mThisObject, rv);
      break;
    case nsIDocument::eAttributeChanged:
      static_cast<LifecycleAttributeChangedCallback *>(mCallback.get())->Call(mThisObject,
        mArgs.name, mArgs.oldValue, mArgs.newValue, rv);
      break;
  }
}

void
CustomElementCallback::Traverse(nsCycleCollectionTraversalCallback& aCb) const
{
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mThisObject");
  aCb.NoteXPCOMChild(mThisObject);

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mCallback");
  aCb.NoteXPCOMChild(mCallback);
}

CustomElementCallback::CustomElementCallback(Element* aThisObject,
                                             nsIDocument::ElementCallbackType aCallbackType,
                                             mozilla::dom::CallbackFunction* aCallback,
                                             CustomElementData* aOwnerData)
  : mThisObject(aThisObject),
    mCallback(aCallback),
    mType(aCallbackType),
    mOwnerData(aOwnerData)
{
}

CustomElementDefinition::CustomElementDefinition(JSObject* aPrototype,
                                                 nsIAtom* aType,
                                                 nsIAtom* aLocalName,
                                                 LifecycleCallbacks* aCallbacks,
                                                 uint32_t aNamespaceID,
                                                 uint32_t aDocOrder)
  : mPrototype(aPrototype),
    mType(aType),
    mLocalName(aLocalName),
    mCallbacks(aCallbacks),
    mNamespaceID(aNamespaceID),
    mDocOrder(aDocOrder)
{
}

CustomElementData::CustomElementData(nsIAtom* aType)
  : mType(aType),
    mCurrentCallback(-1),
    mElementIsBeingCreated(false),
    mCreatedCallbackInvoked(true),
    mAssociatedMicroTask(-1)
{
}

void
CustomElementData::RunCallbackQueue()
{
  // Note: It's possible to re-enter this method.
  while (static_cast<uint32_t>(++mCurrentCallback) < mCallbackQueue.Length()) {
    mCallbackQueue[mCurrentCallback]->Call();
  }

  mCallbackQueue.Clear();
  mCurrentCallback = -1;
}

} // namespace dom
} // namespace mozilla

void
nsIdentifierMapEntry::FireChangeCallbacks(Element* aOldElement,
                                          Element* aNewElement,
                                          bool aImageOnly)
{
  if (!mChangeCallbacks)
    return;

  for (auto iter = mChangeCallbacks->ConstIter(); !iter.Done(); iter.Next()) {
    nsIdentifierMapEntry::ChangeCallbackEntry* entry = iter.Get();
    // Don't fire image changes for non-image observers, and don't fire element
    // changes for image observers when an image override is active.
    if (entry->mKey.mForImage ? (mImageElement && !aImageOnly) : aImageOnly) {
      continue;
    }

    if (!entry->mKey.mCallback(aOldElement, aNewElement, entry->mKey.mData)) {
      iter.Remove();
    }
  }
}

namespace {

struct PositionComparator
{
  Element* const mElement;
  explicit PositionComparator(Element* const aElement) : mElement(aElement) {}

  int operator()(void* aElement) const {
    Element* curElement = static_cast<Element*>(aElement);
    if (mElement == curElement) {
      return 0;
    }
    if (nsContentUtils::PositionIsBefore(mElement, curElement)) {
      return -1;
    }
    return 1;
  }
};

} // namespace

bool
nsIdentifierMapEntry::AddIdElement(Element* aElement)
{
  NS_PRECONDITION(aElement, "Must have element");
  NS_PRECONDITION(!mIdContentList.Contains(nullptr),
                  "Why is null in our list?");

#ifdef DEBUG
  Element* currentElement = mIdContentList.SafeElementAt(0);
#endif

  // Common case
  if (mIdContentList.IsEmpty()) {
    if (!mIdContentList.AppendElement(aElement))
      return false;
    NS_ASSERTION(currentElement == nullptr, "How did that happen?");
    FireChangeCallbacks(nullptr, aElement);
    return true;
  }

  // We seem to have multiple content nodes for the same id, or XUL is messing
  // with us.  Search for the right place to insert the content.

  size_t idx;
  if (BinarySearchIf(mIdContentList, 0, mIdContentList.Length(),
                     PositionComparator(aElement), &idx)) {
    // Already in the list, so already in the right spot.  Get out of here.
    // XXXbz this only happens because XUL does all sorts of random
    // UpdateIdTableEntry calls.  Hate, hate, hate!
    return true;
  }

  if (!mIdContentList.InsertElementAt(idx, aElement)) {
    return false;
  }

  if (idx == 0) {
    Element* oldElement = mIdContentList.SafeElementAt(1);
    NS_ASSERTION(currentElement == oldElement, "How did that happen?");
    FireChangeCallbacks(oldElement, aElement);
  }
  return true;
}

void
nsIdentifierMapEntry::RemoveIdElement(Element* aElement)
{
  NS_PRECONDITION(aElement, "Missing element");

  // This should only be called while the document is in an update.
  // Assertions near the call to this method guarantee this.

  // This could fire in OOM situations
  // Only assert this in HTML documents for now as XUL does all sorts of weird
  // crap.
  NS_ASSERTION(!aElement->OwnerDoc()->IsHTMLDocument() ||
               mIdContentList.Contains(aElement),
               "Removing id entry that doesn't exist");

  // XXXbz should this ever Compact() I guess when all the content is gone
  // we'll just get cleaned up in the natural order of things...
  Element* currentElement = mIdContentList.SafeElementAt(0);
  mIdContentList.RemoveElement(aElement);
  if (currentElement == aElement) {
    FireChangeCallbacks(currentElement, mIdContentList.SafeElementAt(0));
  }
}

void
nsIdentifierMapEntry::SetImageElement(Element* aElement)
{
  Element* oldElement = GetImageIdElement();
  mImageElement = aElement;
  Element* newElement = GetImageIdElement();
  if (oldElement != newElement) {
    FireChangeCallbacks(oldElement, newElement, true);
  }
}

void
nsIdentifierMapEntry::AddNameElement(nsINode* aNode, Element* aElement)
{
  if (!mNameContentList) {
    mNameContentList = new nsSimpleContentList(aNode);
  }

  mNameContentList->AppendElement(aElement);
}

void
nsIdentifierMapEntry::RemoveNameElement(Element* aElement)
{
  if (mNameContentList) {
    mNameContentList->RemoveElement(aElement);
  }
}

bool
nsIdentifierMapEntry::HasIdElementExposedAsHTMLDocumentProperty()
{
  Element* idElement = GetIdElement();
  return idElement &&
         nsGenericHTMLElement::ShouldExposeIdAsHTMLDocumentProperty(idElement);
}

size_t
nsIdentifierMapEntry::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const
{
  return nsStringHashKey::SizeOfExcludingThis(aMallocSizeOf);
}

// Helper structs for the content->subdoc map

class SubDocMapEntry : public PLDHashEntryHdr
{
public:
  // Both of these are strong references
  Element *mKey; // must be first, to look like PLDHashEntryStub
  nsIDocument *mSubDocument;
};


/**
 * A struct that holds all the information about a radio group.
 */
struct nsRadioGroupStruct
{
  nsRadioGroupStruct()
    : mRequiredRadioCount(0)
    , mGroupSuffersFromValueMissing(false)
  {}

  /**
   * A strong pointer to the currently selected radio button.
   */
  RefPtr<HTMLInputElement> mSelectedRadioButton;
  nsCOMArray<nsIFormControl> mRadioButtons;
  uint32_t mRequiredRadioCount;
  bool mGroupSuffersFromValueMissing;
};


nsDOMStyleSheetList::nsDOMStyleSheetList(nsIDocument *aDocument)
{
  mLength = -1;
  // Not reference counted to avoid circular references.
  // The document will tell us when its going away.
  mDocument = aDocument;
  mDocument->AddObserver(this);
}

nsDOMStyleSheetList::~nsDOMStyleSheetList()
{
  if (mDocument) {
    mDocument->RemoveObserver(this);
  }
}

NS_IMPL_ISUPPORTS_INHERITED(nsDOMStyleSheetList, StyleSheetList,
                            nsIDocumentObserver,
                            nsIMutationObserver)

uint32_t
nsDOMStyleSheetList::Length()
{
  if (!mDocument) {
    return 0;
  }

  // XXX Find the number and then cache it. We'll use the
  // observer notification to figure out if new ones have
  // been added or removed.
  if (-1 == mLength) {
    mLength = mDocument->GetNumberOfStyleSheets();
  }
  return mLength;
}

CSSStyleSheet*
nsDOMStyleSheetList::IndexedGetter(uint32_t aIndex, bool& aFound)
{
  if (!mDocument || aIndex >= (uint32_t)mDocument->GetNumberOfStyleSheets()) {
    aFound = false;
    return nullptr;
  }

  aFound = true;
  StyleSheetHandle sheet = mDocument->GetStyleSheetAt(aIndex);
  NS_ASSERTION(sheet, "Must have a sheet");

  // XXXheycam Return null until ServoStyleSheet implements the right DOM
  // interfaces.
  if (sheet->IsServo()) {
    NS_ERROR("stylo: can't return a ServoStyleSheet to the DOM yet");
    return nullptr;
  }
  return sheet->AsGecko();
}

void
nsDOMStyleSheetList::NodeWillBeDestroyed(const nsINode *aNode)
{
  mDocument = nullptr;
}

void
nsDOMStyleSheetList::StyleSheetAdded(StyleSheetHandle aStyleSheet,
                                     bool aDocumentSheet)
{
  if (aDocumentSheet && -1 != mLength) {
    mLength++;
  }
}

void
nsDOMStyleSheetList::StyleSheetRemoved(StyleSheetHandle aStyleSheet,
                                       bool aDocumentSheet)
{
  if (aDocumentSheet && -1 != mLength) {
    mLength--;
  }
}

// nsOnloadBlocker implementation
NS_IMPL_ISUPPORTS(nsOnloadBlocker, nsIRequest)

NS_IMETHODIMP
nsOnloadBlocker::GetName(nsACString &aResult)
{
  aResult.AssignLiteral("about:document-onload-blocker");
  return NS_OK;
}

NS_IMETHODIMP
nsOnloadBlocker::IsPending(bool *_retval)
{
  *_retval = true;
  return NS_OK;
}

NS_IMETHODIMP
nsOnloadBlocker::GetStatus(nsresult *status)
{
  *status = NS_OK;
  return NS_OK;
}

NS_IMETHODIMP
nsOnloadBlocker::Cancel(nsresult status)
{
  return NS_OK;
}
NS_IMETHODIMP
nsOnloadBlocker::Suspend(void)
{
  return NS_OK;
}
NS_IMETHODIMP
nsOnloadBlocker::Resume(void)
{
  return NS_OK;
}

NS_IMETHODIMP
nsOnloadBlocker::GetLoadGroup(nsILoadGroup * *aLoadGroup)
{
  *aLoadGroup = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsOnloadBlocker::SetLoadGroup(nsILoadGroup * aLoadGroup)
{
  return NS_OK;
}

NS_IMETHODIMP
nsOnloadBlocker::GetLoadFlags(nsLoadFlags *aLoadFlags)
{
  *aLoadFlags = nsIRequest::LOAD_NORMAL;
  return NS_OK;
}

NS_IMETHODIMP
nsOnloadBlocker::SetLoadFlags(nsLoadFlags aLoadFlags)
{
  return NS_OK;
}

// ==================================================================

nsExternalResourceMap::nsExternalResourceMap()
  : mHaveShutDown(false)
{
}

nsIDocument*
nsExternalResourceMap::RequestResource(nsIURI* aURI,
                                       nsINode* aRequestingNode,
                                       nsDocument* aDisplayDocument,
                                       ExternalResourceLoad** aPendingLoad)
{
  // If we ever start allowing non-same-origin loads here, we might need to do
  // something interesting with aRequestingPrincipal even for the hashtable
  // gets.
  NS_PRECONDITION(aURI, "Must have a URI");
  NS_PRECONDITION(aRequestingNode, "Must have a node");
  *aPendingLoad = nullptr;
  if (mHaveShutDown) {
    return nullptr;
  }

  // First, make sure we strip the ref from aURI.
  nsCOMPtr<nsIURI> clone;
  nsresult rv = aURI->CloneIgnoringRef(getter_AddRefs(clone));
  if (NS_FAILED(rv) || !clone) {
    return nullptr;
  }

  ExternalResource* resource;
  mMap.Get(clone, &resource);
  if (resource) {
    return resource->mDocument;
  }

  RefPtr<PendingLoad> load;
  mPendingLoads.Get(clone, getter_AddRefs(load));
  if (load) {
    load.forget(aPendingLoad);
    return nullptr;
  }

  load = new PendingLoad(aDisplayDocument);

  mPendingLoads.Put(clone, load);

  if (NS_FAILED(load->StartLoad(clone, aRequestingNode))) {
    // Make sure we don't thrash things by trying this load again, since
    // chances are it failed for good reasons (security check, etc).
    AddExternalResource(clone, nullptr, nullptr, aDisplayDocument);
  } else {
    load.forget(aPendingLoad);
  }

  return nullptr;
}

void
nsExternalResourceMap::EnumerateResources(nsIDocument::nsSubDocEnumFunc aCallback,
                                          void* aData)
{
  for (auto iter = mMap.Iter(); !iter.Done(); iter.Next()) {
    nsExternalResourceMap::ExternalResource* resource = iter.UserData();
    if (resource->mDocument && !aCallback(resource->mDocument, aData)) {
      break;
    }
  }
}

void
nsExternalResourceMap::Traverse(nsCycleCollectionTraversalCallback* aCallback) const
{
  // mPendingLoads will get cleared out as the requests complete, so
  // no need to worry about those here.
  for (auto iter = mMap.ConstIter(); !iter.Done(); iter.Next()) {
    nsExternalResourceMap::ExternalResource* resource = iter.UserData();

    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback,
                                       "mExternalResourceMap.mMap entry"
                                       "->mDocument");
    aCallback->NoteXPCOMChild(resource->mDocument);

    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback,
                                       "mExternalResourceMap.mMap entry"
                                       "->mViewer");
    aCallback->NoteXPCOMChild(resource->mViewer);

    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback,
                                       "mExternalResourceMap.mMap entry"
                                       "->mLoadGroup");
    aCallback->NoteXPCOMChild(resource->mLoadGroup);
  }
}

void
nsExternalResourceMap::HideViewers()
{
  for (auto iter = mMap.Iter(); !iter.Done(); iter.Next()) {
    nsCOMPtr<nsIContentViewer> viewer = iter.UserData()->mViewer;
    if (viewer) {
      viewer->Hide();
    }
  }
}

void
nsExternalResourceMap::ShowViewers()
{
  for (auto iter = mMap.Iter(); !iter.Done(); iter.Next()) {
    nsCOMPtr<nsIContentViewer> viewer = iter.UserData()->mViewer;
    if (viewer) {
      viewer->Show();
    }
  }
}

void
TransferZoomLevels(nsIDocument* aFromDoc,
                   nsIDocument* aToDoc)
{
  MOZ_ASSERT(aFromDoc && aToDoc,
             "transferring zoom levels from/to null doc");

  nsIPresShell* fromShell = aFromDoc->GetShell();
  if (!fromShell)
    return;

  nsPresContext* fromCtxt = fromShell->GetPresContext();
  if (!fromCtxt)
    return;

  nsIPresShell* toShell = aToDoc->GetShell();
  if (!toShell)
    return;

  nsPresContext* toCtxt = toShell->GetPresContext();
  if (!toCtxt)
    return;

  toCtxt->SetFullZoom(fromCtxt->GetFullZoom());
  toCtxt->SetBaseMinFontSize(fromCtxt->BaseMinFontSize());
  toCtxt->SetTextZoom(fromCtxt->TextZoom());
}

void
TransferShowingState(nsIDocument* aFromDoc, nsIDocument* aToDoc)
{
  MOZ_ASSERT(aFromDoc && aToDoc,
             "transferring showing state from/to null doc");

  if (aFromDoc->IsShowing()) {
    aToDoc->OnPageShow(true, nullptr);
  }
}

nsresult
nsExternalResourceMap::AddExternalResource(nsIURI* aURI,
                                           nsIContentViewer* aViewer,
                                           nsILoadGroup* aLoadGroup,
                                           nsIDocument* aDisplayDocument)
{
  NS_PRECONDITION(aURI, "Unexpected call");
  NS_PRECONDITION((aViewer && aLoadGroup) || (!aViewer && !aLoadGroup),
                  "Must have both or neither");

  RefPtr<PendingLoad> load;
  mPendingLoads.Get(aURI, getter_AddRefs(load));
  mPendingLoads.Remove(aURI);

  nsresult rv = NS_OK;

  nsCOMPtr<nsIDocument> doc;
  if (aViewer) {
    doc = aViewer->GetDocument();
    NS_ASSERTION(doc, "Must have a document");

    nsCOMPtr<nsIXULDocument> xulDoc = do_QueryInterface(doc);
    if (xulDoc) {
      // We don't handle XUL stuff here yet.
      rv = NS_ERROR_NOT_AVAILABLE;
    } else {
      doc->SetDisplayDocument(aDisplayDocument);

      // Make sure that hiding our viewer will tear down its presentation.
      aViewer->SetSticky(false);

      rv = aViewer->Init(nullptr, nsIntRect(0, 0, 0, 0));
      if (NS_SUCCEEDED(rv)) {
        rv = aViewer->Open(nullptr, nullptr);
      }
    }

    if (NS_FAILED(rv)) {
      doc = nullptr;
      aViewer = nullptr;
      aLoadGroup = nullptr;
    }
  }

  ExternalResource* newResource = new ExternalResource();
  mMap.Put(aURI, newResource);

  newResource->mDocument = doc;
  newResource->mViewer = aViewer;
  newResource->mLoadGroup = aLoadGroup;
  if (doc) {
    TransferZoomLevels(aDisplayDocument, doc);
    TransferShowingState(aDisplayDocument, doc);
  }

  const nsTArray< nsCOMPtr<nsIObserver> > & obs = load->Observers();
  for (uint32_t i = 0; i < obs.Length(); ++i) {
    obs[i]->Observe(doc, "external-resource-document-created", nullptr);
  }

  return rv;
}

NS_IMPL_ISUPPORTS(nsExternalResourceMap::PendingLoad,
                  nsIStreamListener,
                  nsIRequestObserver)

NS_IMETHODIMP
nsExternalResourceMap::PendingLoad::OnStartRequest(nsIRequest *aRequest,
                                                   nsISupports *aContext)
{
  nsExternalResourceMap& map = mDisplayDocument->ExternalResourceMap();
  if (map.HaveShutDown()) {
    return NS_BINDING_ABORTED;
  }

  nsCOMPtr<nsIContentViewer> viewer;
  nsCOMPtr<nsILoadGroup> loadGroup;
  nsresult rv = SetupViewer(aRequest, getter_AddRefs(viewer),
                            getter_AddRefs(loadGroup));

  // Make sure to do this no matter what
  nsresult rv2 = map.AddExternalResource(mURI, viewer, loadGroup,
                                         mDisplayDocument);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (NS_FAILED(rv2)) {
    mTargetListener = nullptr;
    return rv2;
  }

  return mTargetListener->OnStartRequest(aRequest, aContext);
}

nsresult
nsExternalResourceMap::PendingLoad::SetupViewer(nsIRequest* aRequest,
                                                nsIContentViewer** aViewer,
                                                nsILoadGroup** aLoadGroup)
{
  NS_PRECONDITION(!mTargetListener, "Unexpected call to OnStartRequest");
  *aViewer = nullptr;
  *aLoadGroup = nullptr;

  nsCOMPtr<nsIChannel> chan(do_QueryInterface(aRequest));
  NS_ENSURE_TRUE(chan, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(aRequest));
  if (httpChannel) {
    bool requestSucceeded;
    if (NS_FAILED(httpChannel->GetRequestSucceeded(&requestSucceeded)) ||
        !requestSucceeded) {
      // Bail out on this load, since it looks like we have an HTTP error page
      return NS_BINDING_ABORTED;
    }
  }

  nsAutoCString type;
  chan->GetContentType(type);

  nsCOMPtr<nsILoadGroup> loadGroup;
  chan->GetLoadGroup(getter_AddRefs(loadGroup));

  // Give this document its own loadgroup
  nsCOMPtr<nsILoadGroup> newLoadGroup =
        do_CreateInstance(NS_LOADGROUP_CONTRACTID);
  NS_ENSURE_TRUE(newLoadGroup, NS_ERROR_OUT_OF_MEMORY);
  newLoadGroup->SetLoadGroup(loadGroup);

  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  loadGroup->GetNotificationCallbacks(getter_AddRefs(callbacks));

  nsCOMPtr<nsIInterfaceRequestor> newCallbacks =
    new LoadgroupCallbacks(callbacks);
  newLoadGroup->SetNotificationCallbacks(newCallbacks);

  // This is some serious hackery cribbed from docshell
  nsCOMPtr<nsICategoryManager> catMan =
    do_GetService(NS_CATEGORYMANAGER_CONTRACTID);
  NS_ENSURE_TRUE(catMan, NS_ERROR_NOT_AVAILABLE);
  nsXPIDLCString contractId;
  nsresult rv = catMan->GetCategoryEntry("Gecko-Content-Viewers", type.get(),
                                         getter_Copies(contractId));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIDocumentLoaderFactory> docLoaderFactory =
    do_GetService(contractId);
  NS_ENSURE_TRUE(docLoaderFactory, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsIContentViewer> viewer;
  nsCOMPtr<nsIStreamListener> listener;
  rv = docLoaderFactory->CreateInstance("external-resource", chan, newLoadGroup,
                                        type, nullptr, nullptr,
                                        getter_AddRefs(listener),
                                        getter_AddRefs(viewer));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(viewer, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIParser> parser = do_QueryInterface(listener);
  if (!parser) {
    /// We don't want to deal with the various fake documents yet
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  // We can't handle HTML and other weird things here yet.
  nsIContentSink* sink = parser->GetContentSink();
  nsCOMPtr<nsIXMLContentSink> xmlSink = do_QueryInterface(sink);
  if (!xmlSink) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  listener.swap(mTargetListener);
  viewer.forget(aViewer);
  newLoadGroup.forget(aLoadGroup);
  return NS_OK;
}

NS_IMETHODIMP
nsExternalResourceMap::PendingLoad::OnDataAvailable(nsIRequest* aRequest,
                                                    nsISupports* aContext,
                                                    nsIInputStream* aStream,
                                                    uint64_t aOffset,
                                                    uint32_t aCount)
{
  NS_PRECONDITION(mTargetListener, "Shouldn't be getting called!");
  if (mDisplayDocument->ExternalResourceMap().HaveShutDown()) {
    return NS_BINDING_ABORTED;
  }
  return mTargetListener->OnDataAvailable(aRequest, aContext, aStream, aOffset,
                                          aCount);
}

NS_IMETHODIMP
nsExternalResourceMap::PendingLoad::OnStopRequest(nsIRequest* aRequest,
                                                  nsISupports* aContext,
                                                  nsresult aStatus)
{
  // mTargetListener might be null if SetupViewer or AddExternalResource failed
  if (mTargetListener) {
    nsCOMPtr<nsIStreamListener> listener;
    mTargetListener.swap(listener);
    return listener->OnStopRequest(aRequest, aContext, aStatus);
  }

  return NS_OK;
}

nsresult
nsExternalResourceMap::PendingLoad::StartLoad(nsIURI* aURI,
                                              nsINode* aRequestingNode)
{
  NS_PRECONDITION(aURI, "Must have a URI");
  NS_PRECONDITION(aRequestingNode, "Must have a node");

  nsCOMPtr<nsILoadGroup> loadGroup =
    aRequestingNode->OwnerDoc()->GetDocumentLoadGroup();

  nsresult rv = NS_OK;
  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannel(getter_AddRefs(channel),
                     aURI,
                     aRequestingNode,
                     nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_INHERITS,
                     nsIContentPolicy::TYPE_OTHER,
                     loadGroup);
  NS_ENSURE_SUCCESS(rv, rv);

  mURI = aURI;

  return channel->AsyncOpen2(this);
}

NS_IMPL_ISUPPORTS(nsExternalResourceMap::LoadgroupCallbacks,
                  nsIInterfaceRequestor)

#define IMPL_SHIM(_i) \
  NS_IMPL_ISUPPORTS(nsExternalResourceMap::LoadgroupCallbacks::_i##Shim, _i)

IMPL_SHIM(nsILoadContext)
IMPL_SHIM(nsIProgressEventSink)
IMPL_SHIM(nsIChannelEventSink)
IMPL_SHIM(nsISecurityEventSink)
IMPL_SHIM(nsIApplicationCacheContainer)

#undef IMPL_SHIM

#define IID_IS(_i) aIID.Equals(NS_GET_IID(_i))

#define TRY_SHIM(_i)                                                       \
  PR_BEGIN_MACRO                                                           \
    if (IID_IS(_i)) {                                                      \
      nsCOMPtr<_i> real = do_GetInterface(mCallbacks);                     \
      if (!real) {                                                         \
        return NS_NOINTERFACE;                                             \
      }                                                                    \
      nsCOMPtr<_i> shim = new _i##Shim(this, real);                        \
      shim.forget(aSink);                                                  \
      return NS_OK;                                                        \
    }                                                                      \
  PR_END_MACRO

NS_IMETHODIMP
nsExternalResourceMap::LoadgroupCallbacks::GetInterface(const nsIID & aIID,
                                                        void **aSink)
{
  if (mCallbacks &&
      (IID_IS(nsIPrompt) || IID_IS(nsIAuthPrompt) || IID_IS(nsIAuthPrompt2) ||
       IID_IS(nsITabChild))) {
    return mCallbacks->GetInterface(aIID, aSink);
  }

  *aSink = nullptr;

  TRY_SHIM(nsILoadContext);
  TRY_SHIM(nsIProgressEventSink);
  TRY_SHIM(nsIChannelEventSink);
  TRY_SHIM(nsISecurityEventSink);
  TRY_SHIM(nsIApplicationCacheContainer);

  return NS_NOINTERFACE;
}

#undef TRY_SHIM
#undef IID_IS

nsExternalResourceMap::ExternalResource::~ExternalResource()
{
  if (mViewer) {
    mViewer->Close(nullptr);
    mViewer->Destroy();
  }
}

// ==================================================================
// =
// ==================================================================

// If we ever have an nsIDocumentObserver notification for stylesheet title
// changes we should update the list from that instead of overriding
// EnsureFresh.
class nsDOMStyleSheetSetList final : public DOMStringList
{
public:
  explicit nsDOMStyleSheetSetList(nsIDocument* aDocument);

  void Disconnect()
  {
    mDocument = nullptr;
  }

  virtual void EnsureFresh() override;

protected:
  nsIDocument* mDocument;  // Our document; weak ref.  It'll let us know if it
                           // dies.
};

nsDOMStyleSheetSetList::nsDOMStyleSheetSetList(nsIDocument* aDocument)
  : mDocument(aDocument)
{
  NS_ASSERTION(mDocument, "Must have document!");
}

void
nsDOMStyleSheetSetList::EnsureFresh()
{
  MOZ_ASSERT(NS_IsMainThread());

  mNames.Clear();

  if (!mDocument) {
    return; // Spec says "no exceptions", and we have no style sets if we have
            // no document, for sure
  }

  int32_t count = mDocument->GetNumberOfStyleSheets();
  nsAutoString title;
  for (int32_t index = 0; index < count; index++) {
    StyleSheetHandle sheet = mDocument->GetStyleSheetAt(index);
    NS_ASSERTION(sheet, "Null sheet in sheet list!");
    // XXXheycam ServoStyleSheets don't expose their title yet.
    if (sheet->IsServo()) {
      NS_ERROR("stylo: ServoStyleSets don't expose their title yet");
      continue;
    }
    sheet->AsGecko()->GetTitle(title);
    if (!title.IsEmpty() && !mNames.Contains(title) && !Add(title)) {
      return;
    }
  }
}

// ==================================================================
nsIDocument::SelectorCache::SelectorCache()
  : nsExpirationTracker<SelectorCacheKey, 4>(1000, "nsIDocument::SelectorCache")
{ }

// CacheList takes ownership of aSelectorList.
void nsIDocument::SelectorCache::CacheList(const nsAString& aSelector,
                                           nsCSSSelectorList* aSelectorList)
{
  SelectorCacheKey* key = new SelectorCacheKey(aSelector);
  mTable.Put(key->mKey, aSelectorList);
  AddObject(key);
}

class nsIDocument::SelectorCacheKeyDeleter final : public Runnable
{
public:
  explicit SelectorCacheKeyDeleter(SelectorCacheKey* aToDelete)
    : mSelector(aToDelete)
  {
    MOZ_COUNT_CTOR(SelectorCacheKeyDeleter);
  }

protected:
  ~SelectorCacheKeyDeleter()
  {
    MOZ_COUNT_DTOR(SelectorCacheKeyDeleter);
  }

public:
  NS_IMETHOD Run()
  {
    return NS_OK;
  }

private:
  nsAutoPtr<SelectorCacheKey> mSelector;
};

void nsIDocument::SelectorCache::NotifyExpired(SelectorCacheKey* aSelector)
{
  RemoveObject(aSelector);
  mTable.Remove(aSelector->mKey);
  nsCOMPtr<nsIRunnable> runnable = new SelectorCacheKeyDeleter(aSelector);
  NS_DispatchToCurrentThread(runnable);
}


struct nsIDocument::FrameRequest
{
  FrameRequest(FrameRequestCallback& aCallback,
               int32_t aHandle) :
    mCallback(&aCallback),
    mHandle(aHandle)
  {}

  // Conversion operator so that we can append these to a
  // FrameRequestCallbackList
  operator const RefPtr<FrameRequestCallback>& () const {
    return mCallback;
  }

  // Comparator operators to allow RemoveElementSorted with an
  // integer argument on arrays of FrameRequest
  bool operator==(int32_t aHandle) const {
    return mHandle == aHandle;
  }
  bool operator<(int32_t aHandle) const {
    return mHandle < aHandle;
  }

  RefPtr<FrameRequestCallback> mCallback;
  int32_t mHandle;
};

static already_AddRefed<mozilla::dom::NodeInfo> nullNodeInfo;

// ==================================================================
// =
// ==================================================================
nsIDocument::nsIDocument()
  : nsINode(nullNodeInfo),
    mReferrerPolicySet(false),
    mReferrerPolicy(mozilla::net::RP_Default),
    mBlockAllMixedContent(false),
    mBlockAllMixedContentPreloads(false),
    mUpgradeInsecureRequests(false),
    mUpgradeInsecurePreloads(false),
    mCharacterSet(NS_LITERAL_CSTRING("ISO-8859-1")),
    mNodeInfoManager(nullptr),
    mCompatMode(eCompatibility_FullStandards),
    mVisibilityState(dom::VisibilityState::Hidden),
    mIsInitialDocumentInWindow(false),
    mMayStartLayout(true),
    mVisible(true),
    mRemovedFromDocShell(false),
    // mAllowDNSPrefetch starts true, so that we can always reliably && it
    // with various values that might disable it.  Since we never prefetch
    // unless we get a window, and in that case the docshell value will get
    // &&-ed in, this is safe.
    mAllowDNSPrefetch(true),
    mIsBeingUsedAsImage(false),
    mHasLinksToUpdate(false),
    mFontFaceSetDirty(true),
    mGetUserFontSetCalled(false),
    mPostedFlushUserFontSet(false),
    mPartID(0),
    mDidFireDOMContentLoaded(true),
    mHasScrollLinkedEffect(false),
    mUserHasInteracted(false)
{
  SetInDocument();

  PR_INIT_CLIST(&mDOMMediaQueryLists);
}

// NOTE! nsDocument::operator new() zeroes out all members, so don't
// bother initializing members to 0.

nsDocument::nsDocument(const char* aContentType)
  : nsIDocument()
  , mAnimatingImages(true)
  , mViewportType(Unknown)
{
  SetContentTypeInternal(nsDependentCString(aContentType));

  if (gDocumentLeakPRLog)
    MOZ_LOG(gDocumentLeakPRLog, LogLevel::Debug,
           ("DOCUMENT %p created", this));

  // Start out mLastStyleSheetSet as null, per spec
  SetDOMStringToNull(mLastStyleSheetSet);

  // void state used to differentiate an empty source from an unselected source
  mPreloadPictureFoundSource.SetIsVoid(true);

  if (!sProcessingStack) {
    sProcessingStack.emplace();
    // Add the base queue sentinel to the processing stack.
    sProcessingStack->AppendElement((CustomElementData*) nullptr);
  }
}

void
nsDocument::ClearAllBoxObjects()
{
  if (mBoxObjectTable) {
    for (auto iter = mBoxObjectTable->Iter(); !iter.Done(); iter.Next()) {
      nsPIBoxObject* boxObject = iter.UserData();
      if (boxObject) {
        boxObject->Clear();
      }
    }
    delete mBoxObjectTable;
    mBoxObjectTable = nullptr;
  }
}

nsIDocument::~nsIDocument()
{
  MOZ_ASSERT(PR_CLIST_IS_EMPTY(&mDOMMediaQueryLists),
             "must not have media query lists left");

  if (mNodeInfoManager) {
    mNodeInfoManager->DropDocumentReference();
  }

  UnlinkOriginalDocumentIfStatic();
}

bool
nsDocument::IsAboutPage()
{
  nsCOMPtr<nsIPrincipal> principal = GetPrincipal();
  nsCOMPtr<nsIURI> uri;
  principal->GetURI(getter_AddRefs(uri));
  bool isAboutScheme = true;
  if (uri) {
    uri->SchemeIs("about", &isAboutScheme);
  }
  return isAboutScheme;
}

nsDocument::~nsDocument()
{
  if (gDocumentLeakPRLog)
    MOZ_LOG(gDocumentLeakPRLog, LogLevel::Debug,
           ("DOCUMENT %p destroyed", this));

  NS_ASSERTION(!mIsShowing, "Destroying a currently-showing document");

  // Note: This assert is only non-fatal because mochitest-bc triggers
  // it... as well as the preceding assert about !mIsShowing.
  NS_ASSERTION(!mObservingAppThemeChanged,
               "Document leaked to shutdown, then the observer service dropped "
               "its ref to us so we were able to go away.");

  if (IsTopLevelContentDocument()) {
    //don't report for about: pages
    if (!IsAboutPage()) {
      // Record the page load
      uint32_t pageLoaded = 1;
      Accumulate(Telemetry::MIXED_CONTENT_UNBLOCK_COUNTER, pageLoaded);
      // Record the mixed content status of the docshell in Telemetry
      enum {
        NO_MIXED_CONTENT = 0, // There is no Mixed Content on the page
        MIXED_DISPLAY_CONTENT = 1, // The page attempted to load Mixed Display Content
        MIXED_ACTIVE_CONTENT = 2, // The page attempted to load Mixed Active Content
        MIXED_DISPLAY_AND_ACTIVE_CONTENT = 3 // The page attempted to load Mixed Display & Mixed Active Content
      };

      bool mixedActiveLoaded = GetHasMixedActiveContentLoaded();
      bool mixedActiveBlocked = GetHasMixedActiveContentBlocked();

      bool mixedDisplayLoaded = GetHasMixedDisplayContentLoaded();
      bool mixedDisplayBlocked = GetHasMixedDisplayContentBlocked();

      bool hasMixedDisplay = (mixedDisplayBlocked || mixedDisplayLoaded);
      bool hasMixedActive = (mixedActiveBlocked || mixedActiveLoaded);

      uint32_t mixedContentLevel = NO_MIXED_CONTENT;
      if (hasMixedDisplay && hasMixedActive) {
        mixedContentLevel = MIXED_DISPLAY_AND_ACTIVE_CONTENT;
      } else if (hasMixedActive){
        mixedContentLevel = MIXED_ACTIVE_CONTENT;
      } else if (hasMixedDisplay) {
        mixedContentLevel = MIXED_DISPLAY_CONTENT;
      }
      Accumulate(Telemetry::MIXED_CONTENT_PAGE_LOAD, mixedContentLevel);

      Accumulate(Telemetry::SCROLL_LINKED_EFFECT_FOUND, mHasScrollLinkedEffect);

      // record mixed object subrequest telemetry
      if (mHasMixedContentObjectSubrequest) {
        /* mixed object subrequest loaded on page*/
        Accumulate(Telemetry::MIXED_CONTENT_OBJECT_SUBREQUEST, 1);
      } else {
        /* no mixed object subrequests loaded on page*/
        Accumulate(Telemetry::MIXED_CONTENT_OBJECT_SUBREQUEST, 0);
      }

      // record CSP telemetry on this document
      if (mHasCSP) {
        Accumulate(Telemetry::CSP_DOCUMENTS_COUNT, 1);
      }
      if (mHasUnsafeInlineCSP) {
        Accumulate(Telemetry::CSP_UNSAFE_INLINE_DOCUMENTS_COUNT, 1);
      }
      if (mHasUnsafeEvalCSP) {
        Accumulate(Telemetry::CSP_UNSAFE_EVAL_DOCUMENTS_COUNT, 1);
      }
    }
  }

  ReportUseCounters();

  mInDestructor = true;
  mInUnlinkOrDeletion = true;

  mRegistry = nullptr;

  mozilla::DropJSObjects(this);

  // Clear mObservers to keep it in sync with the mutationobserver list
  mObservers.Clear();

  if (mStyleSheetSetList) {
    mStyleSheetSetList->Disconnect();
  }

  if (mAnimationController) {
    mAnimationController->Disconnect();
  }

  mParentDocument = nullptr;

  // Kill the subdocument map, doing this will release its strong
  // references, if any.
  delete mSubDocuments;
  mSubDocuments = nullptr;

  // Destroy link map now so we don't waste time removing
  // links one by one
  DestroyElementMaps();

  nsAutoScriptBlocker scriptBlocker;

  for (uint32_t indx = mChildren.ChildCount(); indx-- != 0; ) {
    mChildren.ChildAt(indx)->UnbindFromTree();
    mChildren.RemoveChildAt(indx);
  }
  mFirstChild = nullptr;
  mCachedRootElement = nullptr;

  // Let the stylesheets know we're going away
  for (StyleSheetHandle sheet : mStyleSheets) {
    sheet->SetOwningDocument(nullptr);
  }
  if (mAttrStyleSheet) {
    mAttrStyleSheet->SetOwningDocument(nullptr);
  }
  // We don't own the mOnDemandBuiltInUASheets, so we don't need to reset them.

  if (mListenerManager) {
    mListenerManager->Disconnect();
    UnsetFlags(NODE_HAS_LISTENERMANAGER);
  }

  if (mScriptLoader) {
    mScriptLoader->DropDocumentReference();
  }

  if (mCSSLoader) {
    // Could be null here if Init() failed or if we have been unlinked.
    mCSSLoader->DropDocumentReference();
  }

  if (mStyleImageLoader) {
    mStyleImageLoader->DropDocumentReference();
  }

  delete mHeaderData;

  ClearAllBoxObjects();

  mPendingTitleChangeEvent.Revoke();

  // We don't want to leave residual locks on images. Make sure we're in an
  // unlocked state, and then clear the table.
  SetImageLockingState(false);
  mImageTracker.Clear();

  mPlugins.Clear();
}

NS_INTERFACE_TABLE_HEAD(nsDocument)
  NS_WRAPPERCACHE_INTERFACE_TABLE_ENTRY
  NS_INTERFACE_TABLE_BEGIN
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(nsDocument, nsISupports, nsINode)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsINode)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsIDocument)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsIDOMDocument)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsIDOMNode)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsIDOMDocumentXBL)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsIScriptObjectPrincipal)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsIDOMEventTarget)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, mozilla::dom::EventTarget)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsISupportsWeakReference)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsIRadioGroupContainer)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsIMutationObserver)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsIApplicationCacheContainer)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsIObserver)
    NS_INTERFACE_TABLE_ENTRY(nsDocument, nsIDOMXPathEvaluator)
  NS_INTERFACE_TABLE_END
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(nsDocument)
NS_INTERFACE_MAP_END


NS_IMPL_CYCLE_COLLECTING_ADDREF(nsDocument)
NS_IMETHODIMP_(MozExternalRefCountType)
nsDocument::Release()
{
  NS_PRECONDITION(0 != mRefCnt, "dup release");
  NS_ASSERT_OWNINGTHREAD(nsDocument);
  nsISupports* base = NS_CYCLE_COLLECTION_CLASSNAME(nsDocument)::Upcast(this);
  bool shouldDelete = false;
  nsrefcnt count = mRefCnt.decr(base, &shouldDelete);
  NS_LOG_RELEASE(this, count, "nsDocument");
  if (count == 0) {
    if (mStackRefCnt && !mNeedsReleaseAfterStackRefCntRelease) {
      mNeedsReleaseAfterStackRefCntRelease = true;
      NS_ADDREF_THIS();
      return mRefCnt.get();
    }
    mRefCnt.incr(base);
    nsNodeUtils::LastRelease(this);
    mRefCnt.decr(base);
    if (shouldDelete) {
      mRefCnt.stabilizeForDeletion();
      DeleteCycleCollectable();
    }
  }
  return count;
}

NS_IMETHODIMP_(void)
nsDocument::DeleteCycleCollectable()
{
  delete this;
}

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(nsDocument)
  if (Element::CanSkip(tmp, aRemovingAllowed)) {
    EventListenerManager* elm = tmp->GetExistingListenerManager();
    if (elm) {
      elm->MarkForCC();
    }
    if (tmp->mExpandoAndGeneration.expando.isObject()) {
      JS::ExposeObjectToActiveJS(
        &(tmp->mExpandoAndGeneration.expando.toObject()));
    }
    return true;
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(nsDocument)
  return Element::CanSkipInCC(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(nsDocument)
  return Element::CanSkipThis(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

static const char* kNSURIs[] = {
  "([none])",
  "(xmlns)",
  "(xml)",
  "(xhtml)",
  "(XLink)",
  "(XSLT)",
  "(XBL)",
  "(MathML)",
  "(RDF)",
  "(XUL)"
};

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INTERNAL(nsDocument)
  if (MOZ_UNLIKELY(cb.WantDebugInfo())) {
    char name[512];
    nsAutoCString loadedAsData;
    if (tmp->IsLoadedAsData()) {
      loadedAsData.AssignLiteral("data");
    } else {
      loadedAsData.AssignLiteral("normal");
    }
    uint32_t nsid = tmp->GetDefaultNamespaceID();
    nsAutoCString uri;
    if (tmp->mDocumentURI)
      tmp->mDocumentURI->GetSpec(uri);
    if (nsid < ArrayLength(kNSURIs)) {
      snprintf_literal(name, "nsDocument %s %s %s",
                       loadedAsData.get(), kNSURIs[nsid], uri.get());
    }
    else {
      snprintf_literal(name, "nsDocument %s %s",
                       loadedAsData.get(), uri.get());
    }
    cb.DescribeRefCountedNode(tmp->mRefCnt.get(), name);
  }
  else {
    NS_IMPL_CYCLE_COLLECTION_DESCRIBE(nsDocument, tmp->mRefCnt.get())
  }

  // Always need to traverse script objects, so do that before we check
  // if we're uncollectable.
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS

  if (!nsINode::Traverse(tmp, cb)) {
    return NS_SUCCESS_INTERRUPTED_TRAVERSE;
  }

  for (auto iter = tmp->mIdentifierMap.ConstIter(); !iter.Done();
       iter.Next()) {
    iter.Get()->Traverse(&cb);
  }

  tmp->mExternalResourceMap.Traverse(&cb);

  // Traverse the mChildren nsAttrAndChildArray.
  for (int32_t indx = int32_t(tmp->mChildren.ChildCount()); indx > 0; --indx) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mChildren[i]");
    cb.NoteXPCOMChild(tmp->mChildren.ChildAt(indx - 1));
  }

  // Traverse all nsIDocument pointer members.
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSecurityInfo)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDisplayDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFontFaceSet)

  // Traverse all nsDocument nsCOMPtrs.
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParser)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mScriptGlobalObject)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mListenerManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDOMStyleSheets)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mStyleSheetSetList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mScriptLoader)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMasterDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mImportManager)

  for (auto iter = tmp->mRadioGroups.Iter(); !iter.Done(); iter.Next()) {
    nsRadioGroupStruct* radioGroup = iter.UserData();
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(
      cb, "mRadioGroups entry->mSelectedRadioButton");
    cb.NoteXPCOMChild(ToSupports(radioGroup->mSelectedRadioButton));

    uint32_t i, count = radioGroup->mRadioButtons.Count();
    for (i = 0; i < count; ++i) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(
        cb, "mRadioGroups entry->mRadioButtons[i]");
      cb.NoteXPCOMChild(radioGroup->mRadioButtons[i]);
    }
  }

  // The boxobject for an element will only exist as long as it's in the
  // document, so we'll traverse the table here instead of from the element.
  if (tmp->mBoxObjectTable) {
    for (auto iter = tmp->mBoxObjectTable->Iter(); !iter.Done(); iter.Next()) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mBoxObjectTable entry");
      cb.NoteXPCOMChild(iter.UserData());
    }
  }

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mChannel)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mStyleAttrStyleSheet)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mXPathEvaluator)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLayoutHistoryState)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOnloadBlocker)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFirstBaseNodeWithHref)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDOMImplementation)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mImageMaps)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOrientationPendingPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOriginalDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCachedEncoder)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mStateObjectCached)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mUndoManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocumentTimeline)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingAnimationTracker)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTemplateContentsOwner)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mChildrenCollection)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRegistry)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAnonymousContents)

  // Traverse all our nsCOMArrays.
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mStyleSheets)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOnDemandBuiltInUASheets)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPreloadingImages)

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSubImportLinks)

  for (uint32_t i = 0; i < tmp->mFrameRequestCallbacks.Length(); ++i) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mFrameRequestCallbacks[i]");
    cb.NoteXPCOMChild(tmp->mFrameRequestCallbacks[i].mCallback);
  }

  // Traverse animation components
  if (tmp->mAnimationController) {
    tmp->mAnimationController->Traverse(&cb);
  }

  if (tmp->mSubDocuments) {
    for (auto iter = tmp->mSubDocuments->Iter(); !iter.Done(); iter.Next()) {
      auto entry = static_cast<SubDocMapEntry*>(iter.Get());

      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb,
                                         "mSubDocuments entry->mKey");
      cb.NoteXPCOMChild(entry->mKey);
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb,
                                         "mSubDocuments entry->mSubDocument");
      cb.NoteXPCOMChild(entry->mSubDocument);
    }
  }

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCSSLoader)

  // We own only the items in mDOMMediaQueryLists that have listeners;
  // this reference is managed by their AddListener and RemoveListener
  // methods.
  for (PRCList *l = PR_LIST_HEAD(&tmp->mDOMMediaQueryLists);
       l != &tmp->mDOMMediaQueryLists; l = PR_NEXT_LINK(l)) {
    MediaQueryList *mql = static_cast<MediaQueryList*>(l);
    if (mql->HasListeners()) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mDOMMediaQueryLists item");
      cb.NoteXPCOMChild(mql);
    }
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_CLASS(nsDocument)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(nsDocument)
  if (tmp->PreservingWrapper()) {
    NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mExpandoAndGeneration.expando)
  }
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END


NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsDocument)
  tmp->mInUnlinkOrDeletion = true;

  // Clear out our external resources
  tmp->mExternalResourceMap.Shutdown();

  nsAutoScriptBlocker scriptBlocker;

  nsINode::Unlink(tmp);

  // Unlink the mChildren nsAttrAndChildArray.
  for (int32_t indx = int32_t(tmp->mChildren.ChildCount()) - 1;
       indx >= 0; --indx) {
    tmp->mChildren.ChildAt(indx)->UnbindFromTree();
    tmp->mChildren.RemoveChildAt(indx);
  }
  tmp->mFirstChild = nullptr;

  tmp->UnlinkOriginalDocumentIfStatic();

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mXPathEvaluator)
  tmp->mCachedRootElement = nullptr; // Avoid a dangling pointer
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDisplayDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFirstBaseNodeWithHref)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDOMImplementation)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mImageMaps)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCachedEncoder)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mUndoManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocumentTimeline)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingAnimationTracker)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTemplateContentsOwner)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mChildrenCollection)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRegistry)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mMasterDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOrientationPendingPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mImportManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSubImportLinks)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFontFaceSet)

  tmp->mParentDocument = nullptr;

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPreloadingImages)

  tmp->ClearAllBoxObjects();

  if (tmp->mListenerManager) {
    tmp->mListenerManager->Disconnect();
    tmp->UnsetFlags(NODE_HAS_LISTENERMANAGER);
    tmp->mListenerManager = nullptr;
  }

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDOMStyleSheets)

  if (tmp->mStyleSheetSetList) {
    tmp->mStyleSheetSetList->Disconnect();
    tmp->mStyleSheetSetList = nullptr;
  }

  delete tmp->mSubDocuments;
  tmp->mSubDocuments = nullptr;

  tmp->mFrameRequestCallbacks.Clear();

  tmp->mRadioGroups.Clear();

  // nsDocument has a pretty complex destructor, so we're going to
  // assume that *most* cycles you actually want to break somewhere
  // else, and not unlink an awful lot here.

  tmp->mIdentifierMap.Clear();
  tmp->mExpandoAndGeneration.Unlink();

  if (tmp->mAnimationController) {
    tmp->mAnimationController->Unlink();
  }

  tmp->mPendingTitleChangeEvent.Revoke();

  if (tmp->mCSSLoader) {
    tmp->mCSSLoader->DropDocumentReference();
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mCSSLoader)
  }

  // We own only the items in mDOMMediaQueryLists that have listeners;
  // this reference is managed by their AddListener and RemoveListener
  // methods.
  for (PRCList *l = PR_LIST_HEAD(&tmp->mDOMMediaQueryLists);
       l != &tmp->mDOMMediaQueryLists; ) {
    PRCList *next = PR_NEXT_LINK(l);
    MediaQueryList *mql = static_cast<MediaQueryList*>(l);
    mql->RemoveAllListeners();
    l = next;
  }

  tmp->mInUnlinkOrDeletion = false;
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

nsresult
nsDocument::Init()
{
  if (mCSSLoader || mStyleImageLoader || mNodeInfoManager || mScriptLoader) {
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  // Force initialization.
  nsINode::nsSlots* slots = Slots();

  // Prepend self as mutation-observer whether we need it or not (some
  // subclasses currently do, other don't). This is because the code in
  // nsNodeUtils always notifies the first observer first, expecting the
  // first observer to be the document.
  NS_ENSURE_TRUE(slots->mMutationObservers.PrependElementUnlessExists(static_cast<nsIMutationObserver*>(this)),
                 NS_ERROR_OUT_OF_MEMORY);


  mOnloadBlocker = new nsOnloadBlocker();
  mCSSLoader = new mozilla::css::Loader(this);
  // Assume we're not quirky, until we know otherwise
  mCSSLoader->SetCompatibilityMode(eCompatibility_FullStandards);

  mStyleImageLoader = new mozilla::css::ImageLoader(this);

  mNodeInfoManager = new nsNodeInfoManager();
  nsresult rv = mNodeInfoManager->Init(this);
  NS_ENSURE_SUCCESS(rv, rv);

  // mNodeInfo keeps NodeInfoManager alive!
  mNodeInfo = mNodeInfoManager->GetDocumentNodeInfo();
  NS_ENSURE_TRUE(mNodeInfo, NS_ERROR_OUT_OF_MEMORY);
  MOZ_ASSERT(mNodeInfo->NodeType() == nsIDOMNode::DOCUMENT_NODE,
             "Bad NodeType in aNodeInfo");

  NS_ASSERTION(OwnerDoc() == this, "Our nodeinfo is busted!");

  // If after creation the owner js global is not set for a document
  // we use the default compartment for this document, instead of creating
  // wrapper in some random compartment when the document is exposed to js
  // via some events.
  nsCOMPtr<nsIGlobalObject> global = xpc::NativeGlobal(xpc::PrivilegedJunkScope());
  NS_ENSURE_TRUE(global, NS_ERROR_FAILURE);
  mScopeObject = do_GetWeakReference(global);
  MOZ_ASSERT(mScopeObject);

  mScriptLoader = new nsScriptLoader(this);

  mozilla::HoldJSObjects(this);

  return NS_OK;
}

void
nsIDocument::DeleteAllProperties()
{
  for (uint32_t i = 0; i < GetPropertyTableCount(); ++i) {
    PropertyTable(i)->DeleteAllProperties();
  }
}

void
nsIDocument::DeleteAllPropertiesFor(nsINode* aNode)
{
  for (uint32_t i = 0; i < GetPropertyTableCount(); ++i) {
    PropertyTable(i)->DeleteAllPropertiesFor(aNode);
  }
}

nsPropertyTable*
nsIDocument::GetExtraPropertyTable(uint16_t aCategory)
{
  NS_ASSERTION(aCategory > 0, "Category 0 should have already been handled");
  while (aCategory >= mExtraPropertyTables.Length() + 1) {
    mExtraPropertyTables.AppendElement(new nsPropertyTable());
  }
  return mExtraPropertyTables[aCategory - 1];
}

bool
nsIDocument::IsVisibleConsideringAncestors() const
{
  const nsIDocument *parent = this;
  do {
    if (!parent->IsVisible()) {
      return false;
    }
  } while ((parent = parent->GetParentDocument()));

  return true;
      }

void
nsDocument::Reset(nsIChannel* aChannel, nsILoadGroup* aLoadGroup)
{
  nsCOMPtr<nsIURI> uri;
  nsCOMPtr<nsIPrincipal> principal;
  if (aChannel) {
    // Note: this code is duplicated in XULDocument::StartDocumentLoad and
    // nsScriptSecurityManager::GetChannelResultPrincipal.
    // Note: this should match nsDocShell::OnLoadingSite
    NS_GetFinalChannelURI(aChannel, getter_AddRefs(uri));

    nsIScriptSecurityManager *securityManager =
      nsContentUtils::GetSecurityManager();
    if (securityManager) {
      securityManager->GetChannelResultPrincipal(aChannel,
                                                 getter_AddRefs(principal));
    }
  }

  ResetToURI(uri, aLoadGroup, principal);

  // Note that, since mTiming does not change during a reset, the
  // navigationStart time remains unchanged and therefore any future new
  // timeline will have the same global clock time as the old one.
  if (mDocumentTimeline) {
    nsRefreshDriver* rd = mPresShell && mPresShell->GetPresContext() ?
                          mPresShell->GetPresContext()->RefreshDriver() :
                          nullptr;
    if (rd) {
      mDocumentTimeline->NotifyRefreshDriverDestroying(rd);
    }
    mDocumentTimeline = nullptr;
  }

  nsCOMPtr<nsIPropertyBag2> bag = do_QueryInterface(aChannel);
  if (bag) {
    nsCOMPtr<nsIURI> baseURI;
    bag->GetPropertyAsInterface(NS_LITERAL_STRING("baseURI"),
                                NS_GET_IID(nsIURI), getter_AddRefs(baseURI));
    if (baseURI) {
      mDocumentBaseURI = baseURI;
      mChromeXHRDocBaseURI = nullptr;
    }
  }

  mChannel = aChannel;
}

void
nsDocument::ResetToURI(nsIURI *aURI, nsILoadGroup *aLoadGroup,
                       nsIPrincipal* aPrincipal)
{
  NS_PRECONDITION(aURI, "Null URI passed to ResetToURI");

  if (gDocumentLeakPRLog && MOZ_LOG_TEST(gDocumentLeakPRLog, LogLevel::Debug)) {
    nsAutoCString spec;
    aURI->GetSpec(spec);
    PR_LogPrint("DOCUMENT %p ResetToURI %s", this, spec.get());
  }

  mSecurityInfo = nullptr;

  mDocumentLoadGroup = nullptr;

  // Delete references to sub-documents and kill the subdocument map,
  // if any. It holds strong references
  delete mSubDocuments;
  mSubDocuments = nullptr;

  // Destroy link map now so we don't waste time removing
  // links one by one
  DestroyElementMaps();

  bool oldVal = mInUnlinkOrDeletion;
  mInUnlinkOrDeletion = true;
  uint32_t count = mChildren.ChildCount();
  { // Scope for update
    MOZ_AUTO_DOC_UPDATE(this, UPDATE_CONTENT_MODEL, true);
    for (int32_t i = int32_t(count) - 1; i >= 0; i--) {
      nsCOMPtr<nsIContent> content = mChildren.ChildAt(i);

      nsIContent* previousSibling = content->GetPreviousSibling();

      if (nsINode::GetFirstChild() == content) {
        mFirstChild = content->GetNextSibling();
      }
      mChildren.RemoveChildAt(i);
      nsNodeUtils::ContentRemoved(this, content, i, previousSibling);
      content->UnbindFromTree();
    }
    mCachedRootElement = nullptr;
  }
  mInUnlinkOrDeletion = oldVal;

  if (!mMasterDocument) {
    // "When creating an import, use the registry of the master document."
    // Note: at this point the mMasterDocument is already set for imports
    // (and only for imports)
    mRegistry = nullptr;
  }

  // Reset our stylesheets
  ResetStylesheetsToURI(aURI);

  // Release the listener manager
  if (mListenerManager) {
    mListenerManager->Disconnect();
    mListenerManager = nullptr;
  }

  // Release the stylesheets list.
  mDOMStyleSheets = nullptr;

  // Release our principal after tearing down the document, rather than before.
  // This ensures that, during teardown, the document and the dying window (which
  // already nulled out its document pointer and cached the principal) have
  // matching principals.
  SetPrincipal(nullptr);

  // Clear the original URI so SetDocumentURI sets it.
  mOriginalURI = nullptr;

  SetDocumentURI(aURI);
  mChromeXHRDocURI = nullptr;
  // If mDocumentBaseURI is null, nsIDocument::GetBaseURI() returns
  // mDocumentURI.
  mDocumentBaseURI = nullptr;
  mChromeXHRDocBaseURI = nullptr;

  if (aLoadGroup) {
    mDocumentLoadGroup = do_GetWeakReference(aLoadGroup);
    // there was an assertion here that aLoadGroup was not null.  This
    // is no longer valid: nsDocShell::SetDocument does not create a
    // load group, and it works just fine

    // XXXbz what does "just fine" mean exactly?  And given that there
    // is no nsDocShell::SetDocument, what is this talking about?
  }

  mLastModified.Truncate();
  // XXXbz I guess we're assuming that the caller will either pass in
  // a channel with a useful type or call SetContentType?
  SetContentTypeInternal(EmptyCString());
  mContentLanguage.Truncate();
  mBaseTarget.Truncate();
  mReferrer.Truncate();

  mXMLDeclarationBits = 0;

  // Now get our new principal
  if (aPrincipal) {
    SetPrincipal(aPrincipal);
  } else {
    nsIScriptSecurityManager *securityManager =
      nsContentUtils::GetSecurityManager();
    if (securityManager) {
      nsCOMPtr<nsILoadContext> loadContext(mDocumentContainer);

      if (!loadContext && aLoadGroup) {
        nsCOMPtr<nsIInterfaceRequestor> cbs;
        aLoadGroup->GetNotificationCallbacks(getter_AddRefs(cbs));
        loadContext = do_GetInterface(cbs);
      }

      MOZ_ASSERT(loadContext,
                 "must have a load context or pass in an explicit principal");

      nsCOMPtr<nsIPrincipal> principal;
      nsresult rv = securityManager->
        GetLoadContextCodebasePrincipal(mDocumentURI, loadContext,
                                        getter_AddRefs(principal));
      if (NS_SUCCEEDED(rv)) {
        SetPrincipal(principal);
      }
    }
  }

  // Refresh the principal on the compartment.
  nsPIDOMWindow* win = GetInnerWindow();
  if (win) {
    nsGlobalWindow::Cast(win)->RefreshCompartmentPrincipal();
  }
}

void
nsDocument::RemoveDocStyleSheetsFromStyleSets()
{
  // The stylesheets should forget us
  for (StyleSheetHandle sheet : Reversed(mStyleSheets)) {
    sheet->SetOwningDocument(nullptr);

    if (sheet->IsApplicable()) {
      nsCOMPtr<nsIPresShell> shell = GetShell();
      if (shell) {
        shell->StyleSet()->RemoveDocStyleSheet(sheet);
      }
    }
    // XXX Tell observers?
  }
}

void
nsDocument::RemoveStyleSheetsFromStyleSets(
    const nsTArray<StyleSheetHandle::RefPtr>& aSheets,
    SheetType aType)
{
  // The stylesheets should forget us
  for (StyleSheetHandle sheet : Reversed(aSheets)) {
    sheet->SetOwningDocument(nullptr);

    if (sheet->IsApplicable()) {
      nsCOMPtr<nsIPresShell> shell = GetShell();
      if (shell) {
        shell->StyleSet()->RemoveStyleSheet(aType, sheet);
      }
    }
    // XXX Tell observers?
  }
}

void
nsDocument::ResetStylesheetsToURI(nsIURI* aURI)
{
  MOZ_ASSERT(aURI);

  mozAutoDocUpdate upd(this, UPDATE_STYLE, true);
  if (mStyleSetFilled) {
    // Skip removing style sheets from the style set if we know we haven't
    // filled the style set.  (This allows us to avoid calling
    // GetStyleBackendType() too early.)
    RemoveDocStyleSheetsFromStyleSets();
    RemoveStyleSheetsFromStyleSets(mOnDemandBuiltInUASheets, SheetType::Agent);
    RemoveStyleSheetsFromStyleSets(mAdditionalSheets[eAgentSheet], SheetType::Agent);
    RemoveStyleSheetsFromStyleSets(mAdditionalSheets[eUserSheet], SheetType::User);
    RemoveStyleSheetsFromStyleSets(mAdditionalSheets[eAuthorSheet], SheetType::Doc);

    if (GetStyleBackendType() == StyleBackendType::Gecko) {
      nsStyleSheetService *sheetService = nsStyleSheetService::GetInstance();
      if (sheetService) {
        RemoveStyleSheetsFromStyleSets(*sheetService->AuthorStyleSheets(), SheetType::Doc);
      }
    } else {
      NS_ERROR("stylo: nsStyleSheetService doesn't handle ServoStyleSheets yet");
    }

    mStyleSetFilled = false;
  }

  // Release all the sheets
  mStyleSheets.Clear();
  mOnDemandBuiltInUASheets.Clear();
  for (auto& sheets : mAdditionalSheets) {
    sheets.Clear();
  }

  // NOTE:  We don't release the catalog sheets.  It doesn't really matter
  // now, but it could in the future -- in which case not releasing them
  // is probably the right thing to do.

  // Now reset our inline style and attribute sheets.
  if (mAttrStyleSheet) {
    mAttrStyleSheet->Reset();
    mAttrStyleSheet->SetOwningDocument(this);
  } else {
    mAttrStyleSheet = new nsHTMLStyleSheet(this);
  }

  if (!mStyleAttrStyleSheet) {
    mStyleAttrStyleSheet = new nsHTMLCSSStyleSheet();
  }

  if (!mSVGAttrAnimationRuleProcessor) {
    mSVGAttrAnimationRuleProcessor =
      new mozilla::SVGAttrAnimationRuleProcessor();
  }

  // Now set up our style sets
  nsCOMPtr<nsIPresShell> shell = GetShell();
  if (shell) {
    FillStyleSet(shell->StyleSet());
  }
}

static void
AppendSheetsToStyleSet(StyleSetHandle aStyleSet,
                       const nsTArray<StyleSheetHandle::RefPtr>& aSheets,
                       SheetType aType)
{
  for (StyleSheetHandle sheet : Reversed(aSheets)) {
    aStyleSet->AppendStyleSheet(aType, sheet);
  }
}


void
nsDocument::FillStyleSet(StyleSetHandle aStyleSet)
{
  NS_PRECONDITION(aStyleSet, "Must have a style set");
  NS_PRECONDITION(aStyleSet->SheetCount(SheetType::Doc) == 0,
                  "Style set already has document sheets?");

  MOZ_ASSERT(!mStyleSetFilled);

  for (StyleSheetHandle sheet : Reversed(mStyleSheets)) {
    if (sheet->IsApplicable()) {
      aStyleSet->AddDocStyleSheet(sheet, this);
    }
  }

  if (aStyleSet->IsGecko()) {
    nsStyleSheetService *sheetService = nsStyleSheetService::GetInstance();
    if (sheetService) {
      for (StyleSheetHandle sheet : *sheetService->AuthorStyleSheets()) {
        aStyleSet->AppendStyleSheet(SheetType::Doc, sheet);
      }
    }

    // Iterate backwards to maintain order
    for (StyleSheetHandle sheet : Reversed(mOnDemandBuiltInUASheets)) {
      if (sheet->IsApplicable()) {
        aStyleSet->PrependStyleSheet(SheetType::Agent, sheet);
      }
    }
  } else {
    NS_ERROR("stylo: nsStyleSheetService doesn't handle ServoStyleSheets yet");
  }

  AppendSheetsToStyleSet(aStyleSet, mAdditionalSheets[eAgentSheet],
                         SheetType::Agent);
  AppendSheetsToStyleSet(aStyleSet, mAdditionalSheets[eUserSheet],
                         SheetType::User);
  AppendSheetsToStyleSet(aStyleSet, mAdditionalSheets[eAuthorSheet],
                         SheetType::Doc);

  mStyleSetFilled = true;
}

static void
WarnIfSandboxIneffective(nsIDocShell* aDocShell,
                         uint32_t aSandboxFlags,
                         nsIChannel* aChannel)
{
  // If the document is sandboxed (via the HTML5 iframe sandbox
  // attribute) and both the allow-scripts and allow-same-origin
  // keywords are supplied, the sandboxed document can call into its
  // parent document and remove its sandboxing entirely - we print a
  // warning to the web console in this case.
  if (aSandboxFlags & SANDBOXED_NAVIGATION &&
      !(aSandboxFlags & SANDBOXED_SCRIPTS) &&
      !(aSandboxFlags & SANDBOXED_ORIGIN)) {
    nsCOMPtr<nsIDocShellTreeItem> parentAsItem;
    aDocShell->GetSameTypeParent(getter_AddRefs(parentAsItem));
    nsCOMPtr<nsIDocShell> parentDocShell = do_QueryInterface(parentAsItem);
    if (!parentDocShell) {
      return;
    }

    // Don't warn if our parent is not the top-level document.
    nsCOMPtr<nsIDocShellTreeItem> grandParentAsItem;
    parentDocShell->GetSameTypeParent(getter_AddRefs(grandParentAsItem));
    if (grandParentAsItem) {
      return;
    }

    nsCOMPtr<nsIChannel> parentChannel;
    parentDocShell->GetCurrentDocumentChannel(getter_AddRefs(parentChannel));
    if (!parentChannel) {
      return;
    }
    nsresult rv = nsContentUtils::CheckSameOrigin(aChannel, parentChannel);
    if (NS_FAILED(rv)) {
      return;
    }

    nsCOMPtr<nsIDocument> parentDocument = do_GetInterface(parentDocShell);
    nsCOMPtr<nsIURI> iframeUri;
    parentChannel->GetURI(getter_AddRefs(iframeUri));
    nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                    NS_LITERAL_CSTRING("Iframe Sandbox"),
                                    parentDocument,
                                    nsContentUtils::eSECURITY_PROPERTIES,
                                    "BothAllowScriptsAndSameOriginPresent",
                                    nullptr, 0, iframeUri);
  }
}

nsresult
nsDocument::StartDocumentLoad(const char* aCommand, nsIChannel* aChannel,
                              nsILoadGroup* aLoadGroup,
                              nsISupports* aContainer,
                              nsIStreamListener **aDocListener,
                              bool aReset, nsIContentSink* aSink)
{
  if (gDocumentLeakPRLog && MOZ_LOG_TEST(gDocumentLeakPRLog, LogLevel::Debug)) {
    nsCOMPtr<nsIURI> uri;
    aChannel->GetURI(getter_AddRefs(uri));
    nsAutoCString spec;
    if (uri)
      uri->GetSpec(spec);
    PR_LogPrint("DOCUMENT %p StartDocumentLoad %s", this, spec.get());
  }

  MOZ_ASSERT(NodePrincipal()->GetAppId() != nsIScriptSecurityManager::UNKNOWN_APP_ID,
             "Document should never have UNKNOWN_APP_ID");

  MOZ_ASSERT(GetReadyStateEnum() == nsIDocument::READYSTATE_UNINITIALIZED,
             "Bad readyState");
  SetReadyStateInternal(READYSTATE_LOADING);

  if (nsCRT::strcmp(kLoadAsData, aCommand) == 0) {
    mLoadedAsData = true;
    // We need to disable script & style loading in this case.
    // We leave them disabled even in EndLoad(), and let anyone
    // who puts the document on display to worry about enabling.

    // Do not load/process scripts when loading as data
    ScriptLoader()->SetEnabled(false);

    // styles
    CSSLoader()->SetEnabled(false); // Do not load/process styles when loading as data
  } else if (nsCRT::strcmp("external-resource", aCommand) == 0) {
    // Allow CSS, but not scripts
    ScriptLoader()->SetEnabled(false);
  }

  mMayStartLayout = false;

  if (aReset) {
    Reset(aChannel, aLoadGroup);
  }

  nsAutoCString contentType;
  nsCOMPtr<nsIPropertyBag2> bag = do_QueryInterface(aChannel);
  if ((bag && NS_SUCCEEDED(bag->GetPropertyAsACString(
                NS_LITERAL_STRING("contentType"), contentType))) ||
      NS_SUCCEEDED(aChannel->GetContentType(contentType))) {
    // XXX this is only necessary for viewsource:
    nsACString::const_iterator start, end, semicolon;
    contentType.BeginReading(start);
    contentType.EndReading(end);
    semicolon = start;
    FindCharInReadable(';', semicolon, end);
    SetContentTypeInternal(Substring(start, semicolon));
  }

  RetrieveRelevantHeaders(aChannel);

  mChannel = aChannel;
  nsCOMPtr<nsIInputStreamChannel> inStrmChan = do_QueryInterface(mChannel);
  if (inStrmChan) {
    bool isSrcdocChannel;
    inStrmChan->GetIsSrcdocChannel(&isSrcdocChannel);
    if (isSrcdocChannel) {
      mIsSrcdocDocument = true;
    }
  }

  // If this document is being loaded by a docshell, copy its sandbox flags
  // to the document. These are immutable after being set here.
  nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(aContainer);

  if (docShell) {
    nsresult rv = docShell->GetSandboxFlags(&mSandboxFlags);
    NS_ENSURE_SUCCESS(rv, rv);
    WarnIfSandboxIneffective(docShell, mSandboxFlags, GetChannel());
  }

  // The CSP directive upgrade-insecure-requests not only applies to the
  // toplevel document, but also to nested documents. Let's propagate that
  // flag from the parent to the nested document.
  nsCOMPtr<nsIDocShellTreeItem> treeItem = this->GetDocShell();
  if (treeItem) {
    nsCOMPtr<nsIDocShellTreeItem> sameTypeParent;
    treeItem->GetSameTypeParent(getter_AddRefs(sameTypeParent));
    if (sameTypeParent) {
      nsIDocument* doc = sameTypeParent->GetDocument();
      mBlockAllMixedContent = doc->GetBlockAllMixedContent(false);
      // if the parent document makes use of block-all-mixed-content
      // then subdocument preloads should always be blocked.
      mBlockAllMixedContentPreloads =
        mBlockAllMixedContent || doc->GetBlockAllMixedContent(true);

      mUpgradeInsecureRequests = doc->GetUpgradeInsecureRequests(false);
      // if the parent document makes use of upgrade-insecure-requests
      // then subdocument preloads should always be upgraded.
      mUpgradeInsecurePreloads =
        mUpgradeInsecureRequests || doc->GetUpgradeInsecureRequests(true);
    }
  }

  // If this is not a data document, set CSP.
  if (!mLoadedAsData) {
    nsresult rv = InitCSP(aChannel);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

void
nsDocument::SendToConsole(nsCOMArray<nsISecurityConsoleMessage>& aMessages)
{
  for (uint32_t i = 0; i < aMessages.Length(); ++i) {
    nsAutoString messageTag;
    aMessages[i]->GetTag(messageTag);

    nsAutoString category;
    aMessages[i]->GetCategory(category);

    nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                    NS_ConvertUTF16toUTF8(category),
                                    this, nsContentUtils::eSECURITY_PROPERTIES,
                                    NS_ConvertUTF16toUTF8(messageTag).get());
  }
}

static nsresult
AppendCSPFromHeader(nsIContentSecurityPolicy* csp,
                    const nsAString& aHeaderValue,
                    bool aReportOnly)
{
  // Need to tokenize the header value since multiple headers could be
  // concatenated into one comma-separated list of policies.
  // See RFC2616 section 4.2 (last paragraph)
  nsresult rv = NS_OK;
  nsCharSeparatedTokenizer tokenizer(aHeaderValue, ',');
  while (tokenizer.hasMoreTokens()) {
      const nsSubstring& policy = tokenizer.nextToken();
      rv = csp->AppendPolicy(policy, aReportOnly, false);
      NS_ENSURE_SUCCESS(rv, rv);
      {
        MOZ_LOG(gCspPRLog, LogLevel::Debug,
                ("CSP refined with policy: \"%s\"",
                NS_ConvertUTF16toUTF8(policy).get()));
      }
  }
  return NS_OK;
}

bool
nsDocument::IsLoopDocument(nsIChannel *aChannel)
{
  nsCOMPtr<nsIURI> chanURI;
  nsresult rv = aChannel->GetOriginalURI(getter_AddRefs(chanURI));
  NS_ENSURE_SUCCESS(rv, false);

  bool isAbout = false;
  bool isLoop = false;
  rv = chanURI->SchemeIs("about", &isAbout);
  NS_ENSURE_SUCCESS(rv, false);
  if (isAbout) {
    nsCOMPtr<nsIURI> loopURI;
    rv = NS_NewURI(getter_AddRefs(loopURI), "about:loopconversation");
    NS_ENSURE_SUCCESS(rv, false);
    rv = chanURI->EqualsExceptRef(loopURI, &isLoop);
    NS_ENSURE_SUCCESS(rv, false);
    if (!isLoop) {
      rv = NS_NewURI(getter_AddRefs(loopURI), "about:looppanel");
      NS_ENSURE_SUCCESS(rv, false);
      rv = chanURI->EqualsExceptRef(loopURI, &isLoop);
      NS_ENSURE_SUCCESS(rv, false);
    }
  }
  return isLoop;
}

void
nsDocument::ApplySettingsFromCSP(bool aSpeculative)
{
  nsresult rv = NS_OK;
  if (!aSpeculative) {
    // 1) apply settings from regular CSP
    nsCOMPtr<nsIContentSecurityPolicy> csp;
    rv = NodePrincipal()->GetCsp(getter_AddRefs(csp));
    NS_ENSURE_SUCCESS_VOID(rv);
    if (csp) {
      // Set up any Referrer Policy specified by CSP
      bool hasReferrerPolicy = false;
      uint32_t referrerPolicy = mozilla::net::RP_Default;
      rv = csp->GetReferrerPolicy(&referrerPolicy, &hasReferrerPolicy);
      NS_ENSURE_SUCCESS_VOID(rv);
      if (hasReferrerPolicy) {
        mReferrerPolicy = static_cast<ReferrerPolicy>(referrerPolicy);
        mReferrerPolicySet = true;
      }
 
      // Set up 'block-all-mixed-content' if not already inherited
      // from the parent context or set by any other CSP.
      if (!mBlockAllMixedContent) {
        rv = csp->GetBlockAllMixedContent(&mBlockAllMixedContent);
        NS_ENSURE_SUCCESS_VOID(rv);
      }
      if (!mBlockAllMixedContentPreloads) {
        mBlockAllMixedContentPreloads = mBlockAllMixedContent;
     }

      // Set up 'upgrade-insecure-requests' if not already inherited
      // from the parent context or set by any other CSP.
      if (!mUpgradeInsecureRequests) {
        rv = csp->GetUpgradeInsecureRequests(&mUpgradeInsecureRequests);
        NS_ENSURE_SUCCESS_VOID(rv);
      }
      if (!mUpgradeInsecurePreloads) {
        mUpgradeInsecurePreloads = mUpgradeInsecureRequests;
      }
    }
    return;
  }

  // 2) apply settings from speculative csp
  nsCOMPtr<nsIContentSecurityPolicy> preloadCsp;
  rv = NodePrincipal()->GetPreloadCsp(getter_AddRefs(preloadCsp));
  NS_ENSURE_SUCCESS_VOID(rv);
  if (preloadCsp) {
    if (!mBlockAllMixedContentPreloads) {
      rv = preloadCsp->GetBlockAllMixedContent(&mBlockAllMixedContentPreloads);
      NS_ENSURE_SUCCESS_VOID(rv);
    }
    if (!mUpgradeInsecurePreloads) {
      rv = preloadCsp->GetUpgradeInsecureRequests(&mUpgradeInsecurePreloads);
      NS_ENSURE_SUCCESS_VOID(rv);
    }
  }
}

nsresult
nsDocument::InitCSP(nsIChannel* aChannel)
{
  nsCOMPtr<nsIContentSecurityPolicy> csp;
  if (!CSPService::sCSPEnabled) {
    MOZ_LOG(gCspPRLog, LogLevel::Debug,
           ("CSP is disabled, skipping CSP init for document %p", this));
    return NS_OK;
  }

  nsAutoCString tCspHeaderValue, tCspROHeaderValue;

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);
  if (!httpChannel) {
    // check baseChannel for CSP when loading a multipart channel
    nsCOMPtr<nsIMultiPartChannel> multipart = do_QueryInterface(aChannel);
    if (multipart) {
      nsCOMPtr<nsIChannel> baseChannel;
      multipart->GetBaseChannel(getter_AddRefs(baseChannel));
      httpChannel = do_QueryInterface(baseChannel);
    }
  }

  if (httpChannel) {
    httpChannel->GetResponseHeader(
        NS_LITERAL_CSTRING("content-security-policy"),
        tCspHeaderValue);

    httpChannel->GetResponseHeader(
        NS_LITERAL_CSTRING("content-security-policy-report-only"),
        tCspROHeaderValue);
  }
  NS_ConvertASCIItoUTF16 cspHeaderValue(tCspHeaderValue);
  NS_ConvertASCIItoUTF16 cspROHeaderValue(tCspROHeaderValue);

  // Figure out if we need to apply an app default CSP or a CSP from an app manifest
  nsIPrincipal* principal = NodePrincipal();

  uint16_t appStatus = principal->GetAppStatus();
  bool applyAppDefaultCSP = false;
  bool applyAppManifestCSP = false;

  nsAutoString appManifestCSP;
  nsAutoString appDefaultCSP;
  if (appStatus != nsIPrincipal::APP_STATUS_NOT_INSTALLED) {
    nsCOMPtr<nsIAppsService> appsService = do_GetService(APPS_SERVICE_CONTRACTID);
    if (appsService) {
      uint32_t appId = principal->GetAppId();
      appsService->GetManifestCSPByLocalId(appId, appManifestCSP);
      if (!appManifestCSP.IsEmpty()) {
        applyAppManifestCSP = true;
      }
      appsService->GetDefaultCSPByLocalId(appId, appDefaultCSP);
      if (!appDefaultCSP.IsEmpty()) {
        applyAppDefaultCSP = true;
      }
    }
  }

  // Check if this is a document from a WebExtension.
  nsString addonId;
  principal->GetAddonId(addonId);
  bool applyAddonCSP = !addonId.IsEmpty();

  // Check if this is part of the Loop/Hello service
  bool applyLoopCSP = IsLoopDocument(aChannel);

  // If there's no CSP to apply, go ahead and return early
  if (!applyAppDefaultCSP &&
      !applyAppManifestCSP &&
      !applyAddonCSP &&
      !applyLoopCSP &&
      cspHeaderValue.IsEmpty() &&
      cspROHeaderValue.IsEmpty()) {
    if (MOZ_LOG_TEST(gCspPRLog, LogLevel::Debug)) {
      nsCOMPtr<nsIURI> chanURI;
      aChannel->GetURI(getter_AddRefs(chanURI));
      nsAutoCString aspec;
      chanURI->GetAsciiSpec(aspec);
      MOZ_LOG(gCspPRLog, LogLevel::Debug,
             ("no CSP for document, %s, %s",
              aspec.get(),
              applyAppDefaultCSP ? "is app" : "not an app"));
    }

    return NS_OK;
  }

  MOZ_LOG(gCspPRLog, LogLevel::Debug, ("Document is an app or CSP header specified %p", this));

  nsresult rv;

  // If Document is an app check to see if we already set CSP and return early
  // if that is indeed the case.
  //
  // In general (see bug 947831), we should not be setting CSP on a principal
  // that aliases another document. For non-app code this is not a problem
  // since we only share the underlying principal with nested browsing
  // contexts for which a header cannot be set (e.g., about:blank and
  // about:srcodoc iframes) and thus won't try to set the CSP again. This
  // check ensures that we do not try to set CSP for an app.
  if (applyAppDefaultCSP || applyAppManifestCSP) {
    nsCOMPtr<nsIContentSecurityPolicy> csp;
    rv = principal->GetCsp(getter_AddRefs(csp));
    NS_ENSURE_SUCCESS(rv, rv);

    if (csp) {
      MOZ_LOG(gCspPRLog, LogLevel::Debug, ("%s %s %s",
           "This document is sharing principal with another document.",
           "Since the document is an app, CSP was already set.",
           "Skipping attempt to set CSP."));
      return NS_OK;
    }
  }

  rv = principal->EnsureCSP(this, getter_AddRefs(csp));
  NS_ENSURE_SUCCESS(rv, rv);

  // ----- if the doc is an app and we want a default CSP, apply it.
  if (applyAppDefaultCSP) {
    csp->AppendPolicy(appDefaultCSP, false, false);
  }

  // ----- if the doc is an app and specifies a CSP in its manifest, apply it.
  if (applyAppManifestCSP) {
    csp->AppendPolicy(appManifestCSP, false, false);
  }

  // ----- if the doc is an addon, apply its CSP.
  if (applyAddonCSP) {
    nsCOMPtr<nsIAddonPolicyService> aps = do_GetService("@mozilla.org/addons/policy-service;1");

    nsAutoString addonCSP;
    rv = aps->GetBaseCSP(addonCSP);
    if (NS_SUCCEEDED(rv)) {
      csp->AppendPolicy(addonCSP, false, false);
    }

    rv = aps->GetAddonCSP(addonId, addonCSP);
    if (NS_SUCCEEDED(rv)) {
      csp->AppendPolicy(addonCSP, false, false);
    }
  }

  // ----- if the doc is part of Loop, apply the loop CSP
  if (applyLoopCSP) {
    nsAdoptingString loopCSP;
    loopCSP = Preferences::GetString("loop.CSP");
    NS_ASSERTION(loopCSP, "Missing loop.CSP preference");
    // If the pref has been removed, we continue without setting a CSP
    if (loopCSP) {
      csp->AppendPolicy(loopCSP, false, false);
    }
  }

  // ----- if there's a full-strength CSP header, apply it.
  if (!cspHeaderValue.IsEmpty()) {
    rv = AppendCSPFromHeader(csp, cspHeaderValue, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // ----- if there's a report-only CSP header, apply it.
  if (!cspROHeaderValue.IsEmpty()) {
    rv = AppendCSPFromHeader(csp, cspROHeaderValue, true);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // ----- Enforce frame-ancestor policy on any applied policies
  nsCOMPtr<nsIDocShell> docShell(mDocumentContainer);
  if (docShell) {
    bool safeAncestry = false;

    // PermitsAncestry sends violation reports when necessary
    rv = csp->PermitsAncestry(docShell, &safeAncestry);

    if (NS_FAILED(rv) || !safeAncestry) {
      MOZ_LOG(gCspPRLog, LogLevel::Debug,
              ("CSP doesn't like frame's ancestry, not loading."));
      // stop!  ERROR page!
      aChannel->Cancel(NS_ERROR_CSP_FRAME_ANCESTOR_VIOLATION);
    }
  }
  ApplySettingsFromCSP(false);
  return NS_OK;
}

void
nsDocument::StopDocumentLoad()
{
  if (mParser) {
    mParserAborted = true;
    mParser->Terminate();
  }
}

void
nsDocument::SetDocumentURI(nsIURI* aURI)
{
  nsCOMPtr<nsIURI> oldBase = GetDocBaseURI();
  mDocumentURI = NS_TryToMakeImmutable(aURI);
  nsIURI* newBase = GetDocBaseURI();

  bool equalBases = false;
  // Changing just the ref of a URI does not change how relative URIs would
  // resolve wrt to it, so we can treat the bases as equal as long as they're
  // equal ignoring the ref.
  if (oldBase && newBase) {
    oldBase->EqualsExceptRef(newBase, &equalBases);
  }
  else {
    equalBases = !oldBase && !newBase;
  }

  // If this is the first time we're setting the document's URI, set the
  // document's original URI.
  if (!mOriginalURI)
    mOriginalURI = mDocumentURI;

  // If changing the document's URI changed the base URI of the document, we
  // need to refresh the hrefs of all the links on the page.
  if (!equalBases) {
    RefreshLinkHrefs();
  }
}

void
nsDocument::SetChromeXHRDocURI(nsIURI* aURI)
{
  mChromeXHRDocURI = aURI;
}

void
nsDocument::SetChromeXHRDocBaseURI(nsIURI* aURI)
{
  mChromeXHRDocBaseURI = aURI;
}

NS_IMETHODIMP
nsDocument::GetLastModified(nsAString& aLastModified)
{
  nsIDocument::GetLastModified(aLastModified);
  return NS_OK;
}

static void
GetFormattedTimeString(PRTime aTime, nsAString& aFormattedTimeString)
{
  PRExplodedTime prtime;
  PR_ExplodeTime(aTime, PR_LocalTimeParameters, &prtime);
  // "MM/DD/YYYY hh:mm:ss"
  char formatedTime[24];
  if (snprintf_literal(formatedTime, "%02d/%02d/%04d %02d:%02d:%02d",
                       prtime.tm_month + 1, prtime.tm_mday, int(prtime.tm_year),
                       prtime.tm_hour     ,  prtime.tm_min,  prtime.tm_sec)) {
    CopyASCIItoUTF16(nsDependentCString(formatedTime), aFormattedTimeString);
  } else {
    // If we for whatever reason failed to find the last modified time
    // (or even the current time), fall back to what NS4.x returned.
    aFormattedTimeString.AssignLiteral(MOZ_UTF16("01/01/1970 00:00:00"));
  }
}

void
nsIDocument::GetLastModified(nsAString& aLastModified) const
{
  if (!mLastModified.IsEmpty()) {
    aLastModified.Assign(mLastModified);
  } else {
    GetFormattedTimeString(PR_Now(), aLastModified);
  }
}

void
nsDocument::AddToNameTable(Element *aElement, nsIAtom* aName)
{
  MOZ_ASSERT(nsGenericHTMLElement::ShouldExposeNameAsHTMLDocumentProperty(aElement),
             "Only put elements that need to be exposed as document['name'] in "
             "the named table.");

  nsIdentifierMapEntry *entry =
    mIdentifierMap.PutEntry(nsDependentAtomString(aName));

  // Null for out-of-memory
  if (entry) {
    if (!entry->HasNameElement() &&
        !entry->HasIdElementExposedAsHTMLDocumentProperty()) {
      ++mExpandoAndGeneration.generation;
    }
    entry->AddNameElement(this, aElement);
  }
}

void
nsDocument::RemoveFromNameTable(Element *aElement, nsIAtom* aName)
{
  // Speed up document teardown
  if (mIdentifierMap.Count() == 0)
    return;

  nsIdentifierMapEntry *entry =
    mIdentifierMap.GetEntry(nsDependentAtomString(aName));
  if (!entry) // Could be false if the element was anonymous, hence never added
    return;

  entry->RemoveNameElement(aElement);
  if (!entry->HasNameElement() &&
      !entry->HasIdElementExposedAsHTMLDocumentProperty()) {
    ++mExpandoAndGeneration.generation;
  }
}

void
nsDocument::AddToIdTable(Element *aElement, nsIAtom* aId)
{
  nsIdentifierMapEntry *entry =
    mIdentifierMap.PutEntry(nsDependentAtomString(aId));

  if (entry) { /* True except on OOM */
    if (nsGenericHTMLElement::ShouldExposeIdAsHTMLDocumentProperty(aElement) &&
        !entry->HasNameElement() &&
        !entry->HasIdElementExposedAsHTMLDocumentProperty()) {
      ++mExpandoAndGeneration.generation;
    }
    entry->AddIdElement(aElement);
  }
}

void
nsDocument::RemoveFromIdTable(Element *aElement, nsIAtom* aId)
{
  NS_ASSERTION(aId, "huhwhatnow?");

  // Speed up document teardown
  if (mIdentifierMap.Count() == 0) {
    return;
  }

  nsIdentifierMapEntry *entry =
    mIdentifierMap.GetEntry(nsDependentAtomString(aId));
  if (!entry) // Can be null for XML elements with changing ids.
    return;

  entry->RemoveIdElement(aElement);
  if (nsGenericHTMLElement::ShouldExposeIdAsHTMLDocumentProperty(aElement) &&
      !entry->HasNameElement() &&
      !entry->HasIdElementExposedAsHTMLDocumentProperty()) {
    ++mExpandoAndGeneration.generation;
  }
  if (entry->IsEmpty()) {
    mIdentifierMap.RemoveEntry(entry);
  }
}

nsIPrincipal*
nsDocument::GetPrincipal()
{
  return NodePrincipal();
}

extern bool sDisablePrefetchHTTPSPref;

void
nsDocument::SetPrincipal(nsIPrincipal *aNewPrincipal)
{
  if (aNewPrincipal && mAllowDNSPrefetch && sDisablePrefetchHTTPSPref) {
    nsCOMPtr<nsIURI> uri;
    aNewPrincipal->GetURI(getter_AddRefs(uri));
    bool isHTTPS;
    if (!uri || NS_FAILED(uri->SchemeIs("https", &isHTTPS)) ||
        isHTTPS) {
      mAllowDNSPrefetch = false;
    }
  }
  mNodeInfoManager->SetDocumentPrincipal(aNewPrincipal);
}

NS_IMETHODIMP
nsDocument::GetApplicationCache(nsIApplicationCache **aApplicationCache)
{
  NS_IF_ADDREF(*aApplicationCache = mApplicationCache);

  return NS_OK;
}

NS_IMETHODIMP
nsDocument::SetApplicationCache(nsIApplicationCache *aApplicationCache)
{
  mApplicationCache = aApplicationCache;

  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetContentType(nsAString& aContentType)
{
  CopyUTF8toUTF16(GetContentTypeInternal(), aContentType);

  return NS_OK;
}

void
nsDocument::SetContentType(const nsAString& aContentType)
{
  NS_ASSERTION(GetContentTypeInternal().IsEmpty() ||
               GetContentTypeInternal().Equals(NS_ConvertUTF16toUTF8(aContentType)),
               "Do you really want to change the content-type?");

  SetContentTypeInternal(NS_ConvertUTF16toUTF8(aContentType));
}

nsresult
nsDocument::GetAllowPlugins(bool * aAllowPlugins)
{
  // First, we ask our docshell if it allows plugins.
  nsCOMPtr<nsIDocShell> docShell(mDocumentContainer);

  if (docShell) {
    docShell->GetAllowPlugins(aAllowPlugins);

    // If the docshell allows plugins, we check whether
    // we are sandboxed and plugins should not be allowed.
    if (*aAllowPlugins)
      *aAllowPlugins = !(mSandboxFlags & SANDBOXED_PLUGINS);
  }

  return NS_OK;
}

already_AddRefed<UndoManager>
nsDocument::GetUndoManager()
{
  Element* rootElement = GetRootElement();
  if (!rootElement) {
    return nullptr;
  }

  if (!mUndoManager) {
    mUndoManager = new UndoManager(rootElement);
  }

  RefPtr<UndoManager> undoManager = mUndoManager;
  return undoManager.forget();
}

bool
nsDocument::IsElementAnimateEnabled(JSContext* /*unused*/, JSObject* /*unused*/)
{
  MOZ_ASSERT(NS_IsMainThread());

  return nsContentUtils::IsCallerChrome() ||
         Preferences::GetBool("dom.animations-api.core.enabled") ||
         Preferences::GetBool("dom.animations-api.element-animate.enabled");
}

bool
nsDocument::IsWebAnimationsEnabled(JSContext* /*unused*/, JSObject* /*unused*/)
{
  MOZ_ASSERT(NS_IsMainThread());

  return nsContentUtils::IsCallerChrome() ||
         Preferences::GetBool("dom.animations-api.core.enabled");
}

DocumentTimeline*
nsDocument::Timeline()
{
  if (!mDocumentTimeline) {
    mDocumentTimeline = new DocumentTimeline(this);
  }

  return mDocumentTimeline;
}

void
nsDocument::GetAnimations(nsTArray<RefPtr<Animation>>& aAnimations)
{
  Element* root = GetRootElement();
  if (!root) {
    return;
  }
  AnimationFilter filter;
  filter.mSubtree = true;
  root->GetAnimations(filter, aAnimations);
}

/* Return true if the document is in the focused top-level window, and is an
 * ancestor of the focused DOMWindow. */
NS_IMETHODIMP
nsDocument::HasFocus(bool* aResult)
{
  ErrorResult rv;
  *aResult = nsIDocument::HasFocus(rv);
  return rv.StealNSResult();
}

bool
nsIDocument::HasFocus(ErrorResult& rv) const
{
  nsIFocusManager* fm = nsFocusManager::GetFocusManager();
  if (!fm) {
    rv.Throw(NS_ERROR_NOT_AVAILABLE);
    return false;
  }

  // Is there a focused DOMWindow?
  nsCOMPtr<nsIDOMWindow> focusedWindow;
  fm->GetFocusedWindow(getter_AddRefs(focusedWindow));
  if (!focusedWindow) {
    return false;
  }

  // Are we an ancestor of the focused DOMWindow?
  nsCOMPtr<nsIDOMDocument> domDocument;
  focusedWindow->GetDocument(getter_AddRefs(domDocument));
  nsCOMPtr<nsIDocument> document = do_QueryInterface(domDocument);

  for (nsIDocument* currentDoc = document; currentDoc;
       currentDoc = currentDoc->GetParentDocument()) {
    if (currentDoc == this) {
      // Yes, we are an ancestor
      return true;
    }
  }

  return false;
}

NS_IMETHODIMP
nsDocument::GetReferrer(nsAString& aReferrer)
{
  nsIDocument::GetReferrer(aReferrer);
  return NS_OK;
}

void
nsIDocument::GetReferrer(nsAString& aReferrer) const
{
  if (mIsSrcdocDocument && mParentDocument)
      mParentDocument->GetReferrer(aReferrer);
  else
    CopyUTF8toUTF16(mReferrer, aReferrer);
}

nsresult
nsIDocument::GetSrcdocData(nsAString &aSrcdocData)
{
  if (mIsSrcdocDocument) {
    nsCOMPtr<nsIInputStreamChannel> inStrmChan = do_QueryInterface(mChannel);
    if (inStrmChan) {
      return inStrmChan->GetSrcdocData(aSrcdocData);
    }
  }
  aSrcdocData = NullString();
  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetActiveElement(nsIDOMElement **aElement)
{
  nsCOMPtr<nsIDOMElement> el(do_QueryInterface(nsIDocument::GetActiveElement()));
  el.forget(aElement);
  return NS_OK;
}

Element*
nsIDocument::GetActiveElement()
{
  // Get the focused element.
  nsCOMPtr<nsPIDOMWindow> window = GetWindow();
  if (window) {
    nsCOMPtr<nsPIDOMWindow> focusedWindow;
    nsIContent* focusedContent =
      nsFocusManager::GetFocusedDescendant(window, false,
                                           getter_AddRefs(focusedWindow));
    // be safe and make sure the element is from this document
    if (focusedContent && focusedContent->OwnerDoc() == this) {
      if (focusedContent->ChromeOnlyAccess()) {
        focusedContent = focusedContent->FindFirstNonChromeOnlyAccessContent();
      }
      if (focusedContent) {
        return focusedContent->AsElement();
      }
    }
  }

  // No focused element anywhere in this document.  Try to get the BODY.
  RefPtr<nsHTMLDocument> htmlDoc = AsHTMLDocument();
  if (htmlDoc) {
    // Because of IE compatibility, return null when html document doesn't have
    // a body.
    return htmlDoc->GetBody();
  }

  // If we couldn't get a BODY, return the root element.
  return GetDocumentElement();
}

NS_IMETHODIMP
nsDocument::GetCurrentScript(nsIDOMElement **aElement)
{
  nsCOMPtr<nsIDOMElement> el(do_QueryInterface(nsIDocument::GetCurrentScript()));
  el.forget(aElement);
  return NS_OK;
}

Element*
nsIDocument::GetCurrentScript()
{
  nsCOMPtr<Element> el(do_QueryInterface(ScriptLoader()->GetCurrentScript()));
  return el;
}

NS_IMETHODIMP
nsDocument::ElementFromPoint(float aX, float aY, nsIDOMElement** aReturn)
{
  Element* el = nsIDocument::ElementFromPoint(aX, aY);
  nsCOMPtr<nsIDOMElement> retval = do_QueryInterface(el);
  retval.forget(aReturn);
  return NS_OK;
}

Element*
nsIDocument::ElementFromPoint(float aX, float aY)
{
  return ElementFromPointHelper(aX, aY, false, true);
}

void
nsIDocument::ElementsFromPoint(float aX, float aY,
                               nsTArray<RefPtr<Element>>& aElements)
{
  ElementsFromPointHelper(aX, aY, nsIDocument::FLUSH_LAYOUT, aElements);
}

Element*
nsDocument::ElementFromPointHelper(float aX, float aY,
                                   bool aIgnoreRootScrollFrame,
                                   bool aFlushLayout)
{
  AutoTArray<RefPtr<Element>, 1> elementArray;
  ElementsFromPointHelper(aX, aY,
                          ((aIgnoreRootScrollFrame ?  nsIDocument::IGNORE_ROOT_SCROLL_FRAME : 0) |
                           (aFlushLayout ? nsIDocument::FLUSH_LAYOUT : 0) |
                           nsIDocument::IS_ELEMENT_FROM_POINT),
                          elementArray);
  if (elementArray.IsEmpty()) {
    return nullptr;
  }
  return elementArray[0];
}

void
nsDocument::ElementsFromPointHelper(float aX, float aY,
                                    uint32_t aFlags,
                                    nsTArray<RefPtr<mozilla::dom::Element>>& aElements)
{
  // As per the the spec, we return null if either coord is negative
  if (!(aFlags & nsIDocument::IGNORE_ROOT_SCROLL_FRAME) && (aX < 0 || aY < 0)) {
    return;
  }

  nscoord x = nsPresContext::CSSPixelsToAppUnits(aX);
  nscoord y = nsPresContext::CSSPixelsToAppUnits(aY);
  nsPoint pt(x, y);

  // Make sure the layout information we get is up-to-date, and
  // ensure we get a root frame (for everything but XUL)
  if (aFlags & nsIDocument::FLUSH_LAYOUT) {
    FlushPendingNotifications(Flush_Layout);
  }

  nsIPresShell *ps = GetShell();
  if (!ps) {
    return;
  }
  nsIFrame *rootFrame = ps->GetRootFrame();

  // XUL docs, unlike HTML, have no frame tree until everything's done loading
  if (!rootFrame) {
    return; // return null to premature XUL callers as a reminder to wait
  }

  nsTArray<nsIFrame*> outFrames;
  // Emulate what GetFrameAtPoint does, since we want all the frames under our
  // point.
  nsLayoutUtils::GetFramesForArea(rootFrame, nsRect(pt, nsSize(1, 1)), outFrames,
    nsLayoutUtils::IGNORE_PAINT_SUPPRESSION | nsLayoutUtils::IGNORE_CROSS_DOC |
    ((aFlags & nsIDocument::IGNORE_ROOT_SCROLL_FRAME) ?  nsLayoutUtils::IGNORE_ROOT_SCROLL_FRAME : 0));

  // Dunno when this would ever happen, as we should at least have a root frame under us?
  if (outFrames.IsEmpty()) {
    return;
  }

  // Used to filter out repeated elements in sequence.
  nsIContent* lastAdded = nullptr;

  for (uint32_t i = 0; i < outFrames.Length(); i++) {
    nsIContent* node = GetContentInThisDocument(outFrames[i]);

    if (!node || !node->IsElement()) {
      // If this helper is called via ElementsFromPoint, we need to make sure
      // our frame is an element. Otherwise return whatever the top frame is
      // even if it isn't the top-painted element.
      if (!(aFlags & nsIDocument::IS_ELEMENT_FROM_POINT)) {
        continue;
      }
      node = node->GetParent();
    }
    if (node && node != lastAdded) {
      aElements.AppendElement(node->AsElement());
      lastAdded = node;
      // If this helper is called via ElementFromPoint, just return the first
      // element we find.
      if (aFlags & nsIDocument::IS_ELEMENT_FROM_POINT) {
        return;
      }
    }
  }
}

nsresult
nsDocument::NodesFromRectHelper(float aX, float aY,
                                float aTopSize, float aRightSize,
                                float aBottomSize, float aLeftSize,
                                bool aIgnoreRootScrollFrame,
                                bool aFlushLayout,
                                nsIDOMNodeList** aReturn)
{
  NS_ENSURE_ARG_POINTER(aReturn);

  nsSimpleContentList* elements = new nsSimpleContentList(this);
  NS_ADDREF(elements);
  *aReturn = elements;

  // Following the same behavior of elementFromPoint,
  // we don't return anything if either coord is negative
  if (!aIgnoreRootScrollFrame && (aX < 0 || aY < 0))
    return NS_OK;

  nscoord x = nsPresContext::CSSPixelsToAppUnits(aX - aLeftSize);
  nscoord y = nsPresContext::CSSPixelsToAppUnits(aY - aTopSize);
  nscoord w = nsPresContext::CSSPixelsToAppUnits(aLeftSize + aRightSize) + 1;
  nscoord h = nsPresContext::CSSPixelsToAppUnits(aTopSize + aBottomSize) + 1;

  nsRect rect(x, y, w, h);

  // Make sure the layout information we get is up-to-date, and
  // ensure we get a root frame (for everything but XUL)
  if (aFlushLayout) {
    FlushPendingNotifications(Flush_Layout);
  }

  nsIPresShell *ps = GetShell();
  NS_ENSURE_STATE(ps);
  nsIFrame *rootFrame = ps->GetRootFrame();

  // XUL docs, unlike HTML, have no frame tree until everything's done loading
  if (!rootFrame)
    return NS_OK; // return nothing to premature XUL callers as a reminder to wait

  AutoTArray<nsIFrame*,8> outFrames;
  nsLayoutUtils::GetFramesForArea(rootFrame, rect, outFrames,
    nsLayoutUtils::IGNORE_PAINT_SUPPRESSION | nsLayoutUtils::IGNORE_CROSS_DOC |
    (aIgnoreRootScrollFrame ? nsLayoutUtils::IGNORE_ROOT_SCROLL_FRAME : 0));

  // Used to filter out repeated elements in sequence.
  nsIContent* lastAdded = nullptr;

  for (uint32_t i = 0; i < outFrames.Length(); i++) {
    nsIContent* node = GetContentInThisDocument(outFrames[i]);

    if (node && !node->IsElement() && !node->IsNodeOfType(nsINode::eTEXT)) {
      // We have a node that isn't an element or a text node,
      // use its parent content instead.
      node = node->GetParent();
    }
    if (node && node != lastAdded) {
      elements->AppendElement(node);
      lastAdded = node;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetElementsByClassName(const nsAString& aClasses,
                                   nsIDOMNodeList** aReturn)
{
  *aReturn = nsIDocument::GetElementsByClassName(aClasses).take();
  return NS_OK;
}

already_AddRefed<nsContentList>
nsIDocument::GetElementsByClassName(const nsAString& aClasses)
{
  return nsContentUtils::GetElementsByClassName(this, aClasses);
}

NS_IMETHODIMP
nsDocument::ReleaseCapture()
{
  nsIDocument::ReleaseCapture();
  return NS_OK;
}

void
nsIDocument::ReleaseCapture() const
{
  // only release the capture if the caller can access it. This prevents a
  // page from stopping a scrollbar grab for example.
  nsCOMPtr<nsINode> node = nsIPresShell::GetCapturingContent();
  if (node && nsContentUtils::CanCallerAccess(node)) {
    nsIPresShell::SetCapturingContent(nullptr, 0);
  }
}

already_AddRefed<nsIURI>
nsIDocument::GetBaseURI(bool aTryUseXHRDocBaseURI) const
{
  nsCOMPtr<nsIURI> uri;
  if (aTryUseXHRDocBaseURI && mChromeXHRDocBaseURI) {
    uri = mChromeXHRDocBaseURI;
  } else {
    uri = GetDocBaseURI();
  }

  return uri.forget();
}

void
nsDocument::SetBaseURI(nsIURI* aURI)
{
  if (!aURI && !mDocumentBaseURI) {
    return;
  }

  // Don't do anything if the URI wasn't actually changed.
  if (aURI && mDocumentBaseURI) {
    bool equalBases = false;
    mDocumentBaseURI->Equals(aURI, &equalBases);
    if (equalBases) {
      return;
    }
  }

  if (aURI) {
    mDocumentBaseURI = NS_TryToMakeImmutable(aURI);
  } else {
    mDocumentBaseURI = nullptr;
  }
  RefreshLinkHrefs();
}

void
nsDocument::GetBaseTarget(nsAString &aBaseTarget)
{
  aBaseTarget = mBaseTarget;
}

void
nsDocument::SetDocumentCharacterSet(const nsACString& aCharSetID)
{
  // XXX it would be a good idea to assert the sanity of the argument,
  // but before we figure out what to do about non-Encoding Standard
  // encodings in the charset menu and in mailnews, assertions are futile.
  if (!mCharacterSet.Equals(aCharSetID)) {
    if (mMasterDocument && !aCharSetID.EqualsLiteral("UTF-8")) {
      // Imports are always UTF-8
      return;
    }
    mCharacterSet = aCharSetID;

    int32_t n = mCharSetObservers.Length();

    for (int32_t i = 0; i < n; i++) {
      nsIObserver* observer = mCharSetObservers.ElementAt(i);

      observer->Observe(static_cast<nsIDocument *>(this), "charset",
                        NS_ConvertASCIItoUTF16(aCharSetID).get());
    }
  }
}

nsresult
nsDocument::AddCharSetObserver(nsIObserver* aObserver)
{
  NS_ENSURE_ARG_POINTER(aObserver);

  NS_ENSURE_TRUE(mCharSetObservers.AppendElement(aObserver), NS_ERROR_FAILURE);

  return NS_OK;
}

void
nsDocument::RemoveCharSetObserver(nsIObserver* aObserver)
{
  mCharSetObservers.RemoveElement(aObserver);
}

void
nsDocument::GetHeaderData(nsIAtom* aHeaderField, nsAString& aData) const
{
  aData.Truncate();
  const nsDocHeaderData* data = mHeaderData;
  while (data) {
    if (data->mField == aHeaderField) {
      aData = data->mData;

      break;
    }
    data = data->mNext;
  }
}

void
nsDocument::SetHeaderData(nsIAtom* aHeaderField, const nsAString& aData)
{
  if (!aHeaderField) {
    NS_ERROR("null headerField");
    return;
  }

  if (!mHeaderData) {
    if (!aData.IsEmpty()) { // don't bother storing empty string
      mHeaderData = new nsDocHeaderData(aHeaderField, aData);
    }
  }
  else {
    nsDocHeaderData* data = mHeaderData;
    nsDocHeaderData** lastPtr = &mHeaderData;
    bool found = false;
    do {  // look for existing and replace
      if (data->mField == aHeaderField) {
        if (!aData.IsEmpty()) {
          data->mData.Assign(aData);
        }
        else {  // don't store empty string
          *lastPtr = data->mNext;
          data->mNext = nullptr;
          delete data;
        }
        found = true;

        break;
      }
      lastPtr = &(data->mNext);
      data = *lastPtr;
    } while (data);

    if (!aData.IsEmpty() && !found) {
      // didn't find, append
      *lastPtr = new nsDocHeaderData(aHeaderField, aData);
    }
  }

  if (aHeaderField == nsGkAtoms::headerContentLanguage) {
    CopyUTF16toUTF8(aData, mContentLanguage);
  }

  if (aHeaderField == nsGkAtoms::headerDefaultStyle) {
    // Only mess with our stylesheets if we don't have a lastStyleSheetSet, per
    // spec.
    if (DOMStringIsNull(mLastStyleSheetSet)) {
      // Calling EnableStyleSheetsForSetInternal, not SetSelectedStyleSheetSet,
      // per spec.  The idea here is that we're changing our preferred set and
      // that shouldn't change the value of lastStyleSheetSet.  Also, we're
      // using the Internal version so we can update the CSSLoader and not have
      // to worry about null strings.
      EnableStyleSheetsForSetInternal(aData, true);
    }
  }

  if (aHeaderField == nsGkAtoms::refresh) {
    // We get into this code before we have a script global yet, so get to
    // our container via mDocumentContainer.
    nsCOMPtr<nsIRefreshURI> refresher(mDocumentContainer);
    if (refresher) {
      // Note: using mDocumentURI instead of mBaseURI here, for consistency
      // (used to just use the current URI of our webnavigation, but that
      // should really be the same thing).  Note that this code can run
      // before the current URI of the webnavigation has been updated, so we
      // can't assert equality here.
      refresher->SetupRefreshURIFromHeader(mDocumentURI, NodePrincipal(),
                                           NS_ConvertUTF16toUTF8(aData));
    }
  }

  if (aHeaderField == nsGkAtoms::headerDNSPrefetchControl &&
      mAllowDNSPrefetch) {
    // Chromium treats any value other than 'on' (case insensitive) as 'off'.
    mAllowDNSPrefetch = aData.IsEmpty() || aData.LowerCaseEqualsLiteral("on");
  }

  if (aHeaderField == nsGkAtoms::viewport ||
      aHeaderField == nsGkAtoms::handheldFriendly ||
      aHeaderField == nsGkAtoms::viewport_minimum_scale ||
      aHeaderField == nsGkAtoms::viewport_maximum_scale ||
      aHeaderField == nsGkAtoms::viewport_initial_scale ||
      aHeaderField == nsGkAtoms::viewport_height ||
      aHeaderField == nsGkAtoms::viewport_width ||
      aHeaderField ==  nsGkAtoms::viewport_user_scalable) {
    mViewportType = Unknown;
  }

  // Referrer policy spec says to ignore any empty referrer policies.
  if (aHeaderField == nsGkAtoms::referrer && !aData.IsEmpty()) {
    ReferrerPolicy policy = mozilla::net::ReferrerPolicyFromString(aData);

    // Referrer policy spec (section 6.1) says that we always use the newest
    // referrer policy we find
    mReferrerPolicy = policy;
    mReferrerPolicySet = true;
  }
}

void
nsDocument::TryChannelCharset(nsIChannel *aChannel,
                              int32_t& aCharsetSource,
                              nsACString& aCharset,
                              nsHtml5TreeOpExecutor* aExecutor)
{
  if (aChannel) {
    nsAutoCString charsetVal;
    nsresult rv = aChannel->GetContentCharset(charsetVal);
    if (NS_SUCCEEDED(rv)) {
      nsAutoCString preferred;
      if(EncodingUtils::FindEncodingForLabel(charsetVal, preferred)) {
        aCharset = preferred;
        aCharsetSource = kCharsetFromChannel;
        return;
      } else if (aExecutor && !charsetVal.IsEmpty()) {
        aExecutor->ComplainAboutBogusProtocolCharset(this);
      }
    }
  }
}

already_AddRefed<nsIPresShell>
nsDocument::CreateShell(nsPresContext* aContext, nsViewManager* aViewManager,
                        StyleSetHandle aStyleSet)
{
  // Don't add anything here.  Add it to |doCreateShell| instead.
  // This exists so that subclasses can pass other values for the 4th
  // parameter some of the time.
  return doCreateShell(aContext, aViewManager, aStyleSet);
}

already_AddRefed<nsIPresShell>
nsDocument::doCreateShell(nsPresContext* aContext,
                          nsViewManager* aViewManager, StyleSetHandle aStyleSet)
{
  NS_ASSERTION(!mPresShell, "We have a presshell already!");

  NS_ENSURE_FALSE(GetBFCacheEntry(), nullptr);

  FillStyleSet(aStyleSet);

  RefPtr<PresShell> shell = new PresShell;
  shell->Init(this, aContext, aViewManager, aStyleSet);

  // Note: we don't hold a ref to the shell (it holds a ref to us)
  mPresShell = shell;

  // Make sure to never paint if we belong to an invisible DocShell.
  nsCOMPtr<nsIDocShell> docShell(mDocumentContainer);
  if (docShell && docShell->IsInvisible())
    shell->SetNeverPainting(true);

  mExternalResourceMap.ShowViewers();

  MaybeRescheduleAnimationFrameNotifications();

  // Now that we have a shell, we might have @font-face rules.
  RebuildUserFontSet();

  return shell.forget();
}

void
nsDocument::MaybeRescheduleAnimationFrameNotifications()
{
  if (!mPresShell || !IsEventHandlingEnabled()) {
    // bail out for now, until one of those conditions changes
    return;
  }

  nsRefreshDriver* rd = mPresShell->GetPresContext()->RefreshDriver();
  if (!mFrameRequestCallbacks.IsEmpty()) {
    rd->ScheduleFrameRequestCallbacks(this);
  }
}

void
nsIDocument::TakeFrameRequestCallbacks(FrameRequestCallbackList& aCallbacks)
{
  aCallbacks.AppendElements(mFrameRequestCallbacks);
  mFrameRequestCallbacks.Clear();
}

bool
nsIDocument::ShouldThrottleFrameRequests()
{
  if (mStaticCloneCount > 0) {
    // Even if we're not visible, a static clone may be, so run at full speed.
    return false;
  }

  if (Hidden()) {
    // We're not visible (probably in a background tab or the bf cache).
    return true;
  }

  if (!mPresShell) {
    return false;  // Can't do anything smarter.
  }

  nsIFrame* frame = mPresShell->GetRootFrame();
  if (!frame) {
    return false;  // Can't do anything smarter.
  }

  nsIFrame* displayRootFrame = nsLayoutUtils::GetDisplayRootFrame(frame);
  if (!displayRootFrame) {
    return false;  // Can't do anything smarter.
  }

  if (!displayRootFrame->DidPaintPresShell(mPresShell)) {
    // We didn't get painted during the last paint, so we're not visible.
    // Throttle. Note that because we have to paint this document at least
    // once to unthrottle it, we will drop one requestAnimationFrame frame
    // when a document that previously wasn't visible scrolls into view. This
    // is acceptable since it would happen outside the viewport on APZ
    // platforms and is unlikely to be human-perceivable on non-APZ platforms.
    return true;
  }

  // We got painted during the last paint, so run at full speed.
  return false;
}

void
nsDocument::DeleteShell()
{
  mExternalResourceMap.HideViewers();
  if (IsEventHandlingEnabled() && !AnimationsPaused()) {
    RevokeAnimationFrameNotifications();
  }

  // When our shell goes away, request that all our images be immediately
  // discarded, so we don't carry around decoded image data for a document we
  // no longer intend to paint.
  for (auto iter = mImageTracker.Iter(); !iter.Done(); iter.Next()) {
    iter.Key()->RequestDiscard();
  }

  // Now that we no longer have a shell, we need to forget about any FontFace
  // objects for @font-face rules that came from the style set.
  RebuildUserFontSet();

  mPresShell = nullptr;
  mStyleSetFilled = false;
}

void
nsDocument::RevokeAnimationFrameNotifications()
{
  if (!mFrameRequestCallbacks.IsEmpty()) {
    mPresShell->GetPresContext()->RefreshDriver()->
      RevokeFrameRequestCallbacks(this);
  }
}

static void
SubDocClearEntry(PLDHashTable *table, PLDHashEntryHdr *entry)
{
  SubDocMapEntry *e = static_cast<SubDocMapEntry *>(entry);

  NS_RELEASE(e->mKey);
  if (e->mSubDocument) {
    e->mSubDocument->SetParentDocument(nullptr);
    NS_RELEASE(e->mSubDocument);
  }
}

static void
SubDocInitEntry(PLDHashEntryHdr *entry, const void *key)
{
  SubDocMapEntry *e =
    const_cast<SubDocMapEntry *>
              (static_cast<const SubDocMapEntry *>(entry));

  e->mKey = const_cast<Element*>(static_cast<const Element*>(key));
  NS_ADDREF(e->mKey);

  e->mSubDocument = nullptr;
}

nsresult
nsDocument::SetSubDocumentFor(Element* aElement, nsIDocument* aSubDoc)
{
  NS_ENSURE_TRUE(aElement, NS_ERROR_UNEXPECTED);

  if (!aSubDoc) {
    // aSubDoc is nullptr, remove the mapping

    if (mSubDocuments) {
      mSubDocuments->Remove(aElement);
    }
  } else {
    if (!mSubDocuments) {
      // Create a new hashtable

      static const PLDHashTableOps hash_table_ops =
      {
        PLDHashTable::HashVoidPtrKeyStub,
        PLDHashTable::MatchEntryStub,
        PLDHashTable::MoveEntryStub,
        SubDocClearEntry,
        SubDocInitEntry
      };

      mSubDocuments = new PLDHashTable(&hash_table_ops, sizeof(SubDocMapEntry));
    }

    // Add a mapping to the hash table
    auto entry =
      static_cast<SubDocMapEntry*>(mSubDocuments->Add(aElement, fallible));

    if (!entry) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    if (entry->mSubDocument) {
      entry->mSubDocument->SetParentDocument(nullptr);

      // Release the old sub document
      NS_RELEASE(entry->mSubDocument);
    }

    entry->mSubDocument = aSubDoc;
    NS_ADDREF(entry->mSubDocument);

    aSubDoc->SetParentDocument(this);
  }

  return NS_OK;
}

nsIDocument*
nsDocument::GetSubDocumentFor(nsIContent *aContent) const
{
  if (mSubDocuments && aContent->IsElement()) {
    auto entry = static_cast<SubDocMapEntry*>
                            (mSubDocuments->Search(aContent->AsElement()));

    if (entry) {
      return entry->mSubDocument;
    }
  }

  return nullptr;
}

Element*
nsDocument::FindContentForSubDocument(nsIDocument *aDocument) const
{
  NS_ENSURE_TRUE(aDocument, nullptr);

  if (!mSubDocuments) {
    return nullptr;
  }

  for (auto iter = mSubDocuments->Iter(); !iter.Done(); iter.Next()) {
    auto entry = static_cast<SubDocMapEntry*>(iter.Get());
    if (entry->mSubDocument == aDocument) {
      return entry->mKey;
    }
  }
  return nullptr;
}

bool
nsDocument::IsNodeOfType(uint32_t aFlags) const
{
    return !(aFlags & ~eDOCUMENT);
}

Element*
nsIDocument::GetRootElement() const
{
  return (mCachedRootElement && mCachedRootElement->GetParentNode() == this) ?
         mCachedRootElement : GetRootElementInternal();
}

Element*
nsDocument::GetRootElementInternal() const
{
  // Loop backwards because any non-elements, such as doctypes and PIs
  // are likely to appear before the root element.
  uint32_t i;
  for (i = mChildren.ChildCount(); i > 0; --i) {
    nsIContent* child = mChildren.ChildAt(i - 1);
    if (child->IsElement()) {
      const_cast<nsDocument*>(this)->mCachedRootElement = child->AsElement();
      return child->AsElement();
    }
  }

  const_cast<nsDocument*>(this)->mCachedRootElement = nullptr;
  return nullptr;
}

nsIContent *
nsDocument::GetChildAt(uint32_t aIndex) const
{
  return mChildren.GetSafeChildAt(aIndex);
}

int32_t
nsDocument::IndexOf(const nsINode* aPossibleChild) const
{
  return mChildren.IndexOfChild(aPossibleChild);
}

uint32_t
nsDocument::GetChildCount() const
{
  return mChildren.ChildCount();
}

nsIContent * const *
nsDocument::GetChildArray(uint32_t* aChildCount) const
{
  return mChildren.GetChildArray(aChildCount);
}


nsresult
nsDocument::InsertChildAt(nsIContent* aKid, uint32_t aIndex,
                          bool aNotify)
{
  if (aKid->IsElement() && GetRootElement()) {
    NS_WARNING("Inserting root element when we already have one");
    return NS_ERROR_DOM_HIERARCHY_REQUEST_ERR;
  }

  return doInsertChildAt(aKid, aIndex, aNotify, mChildren);
}

void
nsDocument::RemoveChildAt(uint32_t aIndex, bool aNotify)
{
  nsCOMPtr<nsIContent> oldKid = GetChildAt(aIndex);
  if (!oldKid) {
    return;
  }

  if (oldKid->IsElement()) {
    // Destroy the link map up front before we mess with the child list.
    DestroyElementMaps();
  }

  doRemoveChildAt(aIndex, aNotify, oldKid, mChildren);
  mCachedRootElement = nullptr;
}

void
nsDocument::EnsureOnDemandBuiltInUASheet(StyleSheetHandle aSheet)
{
  if (mOnDemandBuiltInUASheets.Contains(aSheet)) {
    return;
  }
  BeginUpdate(UPDATE_STYLE);
  AddOnDemandBuiltInUASheet(aSheet);
  EndUpdate(UPDATE_STYLE);
}

void
nsDocument::AddOnDemandBuiltInUASheet(StyleSheetHandle aSheet)
{
  MOZ_ASSERT(!mOnDemandBuiltInUASheets.Contains(aSheet));

  // Prepend here so that we store the sheets in mOnDemandBuiltInUASheets in
  // the same order that they should end up in the style set.
  mOnDemandBuiltInUASheets.InsertElementAt(0, aSheet);

  if (aSheet->IsApplicable()) {
    // This is like |AddStyleSheetToStyleSets|, but for an agent sheet.
    nsCOMPtr<nsIPresShell> shell = GetShell();
    if (shell) {
      // Note that prepending here is necessary to make sure that html.css etc.
      // do not override Firefox OS/Mobile's content.css sheet. Maybe we should
      // have an insertion point to match the order of
      // nsDocumentViewer::CreateStyleSet though?
      shell->StyleSet()->PrependStyleSheet(SheetType::Agent, aSheet);
    }
  }

  NotifyStyleSheetAdded(aSheet, false);
}

int32_t
nsDocument::GetNumberOfStyleSheets() const
{
  return mStyleSheets.Length();
}

StyleSheetHandle
nsDocument::GetStyleSheetAt(int32_t aIndex) const
{
  return mStyleSheets.SafeElementAt(aIndex, StyleSheetHandle());
}

int32_t
nsDocument::GetIndexOfStyleSheet(const StyleSheetHandle aSheet) const
{
  return mStyleSheets.IndexOf(aSheet);
}

void
nsDocument::AddStyleSheetToStyleSets(StyleSheetHandle aSheet)
{
  nsCOMPtr<nsIPresShell> shell = GetShell();
  if (shell) {
    shell->StyleSet()->AddDocStyleSheet(aSheet, this);
  }
}

#define DO_STYLESHEET_NOTIFICATION(className, type, memberName, argName)      \
  do {                                                                        \
    className##Init init;                                                     \
    init.mBubbles = true;                                                     \
    init.mCancelable = true;                                                  \
    /* XXXheycam ServoStyleSheet doesn't implement DOM interfaces yet */      \
    if (aSheet->IsServo()) {                                                  \
      NS_ERROR("stylo: can't dispatch events for ServoStyleSheets yet");      \
    }                                                                         \
    init.mStylesheet = aSheet->IsGecko() ? aSheet->AsGecko() : nullptr;       \
    init.memberName = argName;                                                \
                                                                              \
    RefPtr<className> event =                                               \
      className::Constructor(this, NS_LITERAL_STRING(type), init);            \
    event->SetTrusted(true);                                                  \
    event->SetTarget(this);                                                   \
    RefPtr<AsyncEventDispatcher> asyncDispatcher =                          \
      new AsyncEventDispatcher(this, event);                                  \
    asyncDispatcher->mOnlyChromeDispatch = true;                              \
    asyncDispatcher->PostDOMEvent();                                          \
  } while (0);

void
nsDocument::NotifyStyleSheetAdded(StyleSheetHandle aSheet, bool aDocumentSheet)
{
  NS_DOCUMENT_NOTIFY_OBSERVERS(StyleSheetAdded, (aSheet, aDocumentSheet));

  if (StyleSheetChangeEventsEnabled()) {
    DO_STYLESHEET_NOTIFICATION(StyleSheetChangeEvent,
                               "StyleSheetAdded",
                               mDocumentSheet,
                               aDocumentSheet);
  }
}

void
nsDocument::NotifyStyleSheetRemoved(StyleSheetHandle aSheet, bool aDocumentSheet)
{
  NS_DOCUMENT_NOTIFY_OBSERVERS(StyleSheetRemoved, (aSheet, aDocumentSheet));

  if (StyleSheetChangeEventsEnabled()) {
    DO_STYLESHEET_NOTIFICATION(StyleSheetChangeEvent,
                               "StyleSheetRemoved",
                               mDocumentSheet,
                               aDocumentSheet);
  }
}

void
nsDocument::AddStyleSheet(StyleSheetHandle aSheet)
{
  NS_PRECONDITION(aSheet, "null arg");
  mStyleSheets.AppendElement(aSheet);
  aSheet->SetOwningDocument(this);

  if (aSheet->IsApplicable()) {
    AddStyleSheetToStyleSets(aSheet);
  }

  NotifyStyleSheetAdded(aSheet, true);
}

void
nsDocument::RemoveStyleSheetFromStyleSets(StyleSheetHandle aSheet)
{
  nsCOMPtr<nsIPresShell> shell = GetShell();
  if (shell) {
    shell->StyleSet()->RemoveDocStyleSheet(aSheet);
  }
}

void
nsDocument::RemoveStyleSheet(StyleSheetHandle aSheet)
{
  NS_PRECONDITION(aSheet, "null arg");
  StyleSheetHandle::RefPtr sheet = aSheet; // hold ref so it won't die too soon

  if (!mStyleSheets.RemoveElement(aSheet)) {
    NS_ASSERTION(mInUnlinkOrDeletion, "stylesheet not found");
    return;
  }

  if (!mIsGoingAway) {
    if (aSheet->IsApplicable()) {
      RemoveStyleSheetFromStyleSets(aSheet);
    }

    NotifyStyleSheetRemoved(aSheet, true);
  }

  aSheet->SetOwningDocument(nullptr);
}

void
nsDocument::UpdateStyleSheets(nsTArray<StyleSheetHandle::RefPtr>& aOldSheets,
                              nsTArray<StyleSheetHandle::RefPtr>& aNewSheets)
{
  BeginUpdate(UPDATE_STYLE);

  // XXX Need to set the sheet on the ownernode, if any
  NS_PRECONDITION(aOldSheets.Length() == aNewSheets.Length(),
                  "The lists must be the same length!");
  int32_t count = aOldSheets.Length();

  StyleSheetHandle::RefPtr oldSheet;
  int32_t i;
  for (i = 0; i < count; ++i) {
    oldSheet = aOldSheets[i];

    // First remove the old sheet.
    NS_ASSERTION(oldSheet, "None of the old sheets should be null");
    int32_t oldIndex = mStyleSheets.IndexOf(oldSheet);
    RemoveStyleSheet(oldSheet);  // This does the right notifications

    // Now put the new one in its place.  If it's null, just ignore it.
    StyleSheetHandle newSheet = aNewSheets[i];
    if (newSheet) {
      mStyleSheets.InsertElementAt(oldIndex, newSheet);
      newSheet->SetOwningDocument(this);
      if (newSheet->IsApplicable()) {
        AddStyleSheetToStyleSets(newSheet);
      }

      NotifyStyleSheetAdded(newSheet, true);
    }
  }

  EndUpdate(UPDATE_STYLE);
}

void
nsDocument::InsertStyleSheetAt(StyleSheetHandle aSheet, int32_t aIndex)
{
  NS_PRECONDITION(aSheet, "null ptr");

  mStyleSheets.InsertElementAt(aIndex, aSheet);

  aSheet->SetOwningDocument(this);

  if (aSheet->IsApplicable()) {
    AddStyleSheetToStyleSets(aSheet);
  }

  NotifyStyleSheetAdded(aSheet, true);
}


void
nsDocument::SetStyleSheetApplicableState(StyleSheetHandle aSheet,
                                         bool aApplicable)
{
  NS_PRECONDITION(aSheet, "null arg");

  // If we're actually in the document style sheet list
  if (mStyleSheets.IndexOf(aSheet) != mStyleSheets.NoIndex) {
    if (aApplicable) {
      AddStyleSheetToStyleSets(aSheet);
    } else {
      RemoveStyleSheetFromStyleSets(aSheet);
    }
  }

  // We have to always notify, since this will be called for sheets
  // that are children of sheets in our style set, as well as some
  // sheets for nsHTMLEditor.

  NS_DOCUMENT_NOTIFY_OBSERVERS(StyleSheetApplicableStateChanged, (aSheet));

  if (StyleSheetChangeEventsEnabled()) {
    DO_STYLESHEET_NOTIFICATION(StyleSheetApplicableStateChangeEvent,
                               "StyleSheetApplicableStateChanged",
                               mApplicable,
                               aApplicable);
  }

  if (!mSSApplicableStateNotificationPending) {
    nsCOMPtr<nsIRunnable> notification = NewRunnableMethod(this,
      &nsDocument::NotifyStyleSheetApplicableStateChanged);
    mSSApplicableStateNotificationPending =
      NS_SUCCEEDED(NS_DispatchToCurrentThread(notification));
  }
}

void
nsDocument::NotifyStyleSheetApplicableStateChanged()
{
  mSSApplicableStateNotificationPending = false;
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();
  if (observerService) {
    observerService->NotifyObservers(static_cast<nsIDocument*>(this),
                                     "style-sheet-applicable-state-changed",
                                     nullptr);
  }
}

static SheetType
ConvertAdditionalSheetType(nsIDocument::additionalSheetType aType)
{
  switch(aType) {
    case nsIDocument::eAgentSheet:
      return SheetType::Agent;
    case nsIDocument::eUserSheet:
      return SheetType::User;
    case nsIDocument::eAuthorSheet:
      return SheetType::Doc;
    default:
      MOZ_ASSERT(false, "wrong type");
      // we must return something although this should never happen
      return SheetType::Count;
  }
}

static int32_t
FindSheet(const nsTArray<StyleSheetHandle::RefPtr>& aSheets, nsIURI* aSheetURI)
{
  for (int32_t i = aSheets.Length() - 1; i >= 0; i-- ) {
    bool bEqual;
    nsIURI* uri = aSheets[i]->GetSheetURI();

    if (uri && NS_SUCCEEDED(uri->Equals(aSheetURI, &bEqual)) && bEqual)
      return i;
  }

  return -1;
}

nsresult
nsDocument::LoadAdditionalStyleSheet(additionalSheetType aType,
                                     nsIURI* aSheetURI)
{
  NS_PRECONDITION(aSheetURI, "null arg");

  // Checking if we have loaded this one already.
  if (FindSheet(mAdditionalSheets[aType], aSheetURI) >= 0)
    return NS_ERROR_INVALID_ARG;

  // Loading the sheet sync.
  RefPtr<css::Loader> loader = new css::Loader(GetStyleBackendType());

  css::SheetParsingMode parsingMode;
  switch (aType) {
    case nsIDocument::eAgentSheet:
      parsingMode = css::eAgentSheetFeatures;
      break;

    case nsIDocument::eUserSheet:
      parsingMode = css::eUserSheetFeatures;
      break;

    case nsIDocument::eAuthorSheet:
      parsingMode = css::eAuthorSheetFeatures;
      break;

    default:
      MOZ_CRASH("impossible value for aType");
  }

  StyleSheetHandle::RefPtr sheet;
  nsresult rv = loader->LoadSheetSync(aSheetURI, parsingMode, true, &sheet);
  NS_ENSURE_SUCCESS(rv, rv);

  sheet->SetOwningDocument(this);
  MOZ_ASSERT(sheet->IsApplicable());

  return AddAdditionalStyleSheet(aType, sheet);
}

nsresult
nsDocument::AddAdditionalStyleSheet(additionalSheetType aType, StyleSheetHandle aSheet)
{
  if (mAdditionalSheets[aType].Contains(aSheet))
    return NS_ERROR_INVALID_ARG;

  if (!aSheet->IsApplicable())
    return NS_ERROR_INVALID_ARG;

  mAdditionalSheets[aType].AppendElement(aSheet);

  BeginUpdate(UPDATE_STYLE);
  nsCOMPtr<nsIPresShell> shell = GetShell();
  if (shell) {
    SheetType type = ConvertAdditionalSheetType(aType);
    shell->StyleSet()->AppendStyleSheet(type, aSheet);
  }

  // Passing false, so documet.styleSheets.length will not be affected by
  // these additional sheets.
  NotifyStyleSheetAdded(aSheet, false);
  EndUpdate(UPDATE_STYLE);
  return NS_OK;
}

void
nsDocument::RemoveAdditionalStyleSheet(additionalSheetType aType, nsIURI* aSheetURI)
{
  MOZ_ASSERT(aSheetURI);

  nsTArray<StyleSheetHandle::RefPtr>& sheets = mAdditionalSheets[aType];

  int32_t i = FindSheet(mAdditionalSheets[aType], aSheetURI);
  if (i >= 0) {
    StyleSheetHandle::RefPtr sheetRef = sheets[i];
    sheets.RemoveElementAt(i);

    BeginUpdate(UPDATE_STYLE);
    if (!mIsGoingAway) {
      MOZ_ASSERT(sheetRef->IsApplicable());
      nsCOMPtr<nsIPresShell> shell = GetShell();
      if (shell) {
        SheetType type = ConvertAdditionalSheetType(aType);
        shell->StyleSet()->RemoveStyleSheet(type, sheetRef);
      }
    }

    // Passing false, so documet.styleSheets.length will not be affected by
    // these additional sheets.
    NotifyStyleSheetRemoved(sheetRef, false);
    EndUpdate(UPDATE_STYLE);

    sheetRef->SetOwningDocument(nullptr);
  }
}

StyleSheetHandle
nsDocument::GetFirstAdditionalAuthorSheet()
{
  return mAdditionalSheets[eAuthorSheet].SafeElementAt(0, StyleSheetHandle());
}

nsIGlobalObject*
nsDocument::GetScopeObject() const
{
  nsCOMPtr<nsIGlobalObject> scope(do_QueryReferent(mScopeObject));
  return scope;
}

void
nsDocument::SetScopeObject(nsIGlobalObject* aGlobal)
{
  mScopeObject = do_GetWeakReference(aGlobal);
  if (aGlobal) {
    mHasHadScriptHandlingObject = true;
  }
}

static void
CheckIfContainsMSEContent(nsISupports* aSupports, void* aContainsMSE)
{
  nsCOMPtr<nsIDOMHTMLMediaElement> domMediaElem(do_QueryInterface(aSupports));
  if (domMediaElem) {
    nsCOMPtr<nsIContent> content(do_QueryInterface(domMediaElem));
    MOZ_ASSERT(content, "aSupports is not a content");
    HTMLMediaElement* mediaElem = static_cast<HTMLMediaElement*>(content.get());
    bool* contains = static_cast<bool*>(aContainsMSE);
    RefPtr<MediaSource> ms = mediaElem->GetMozMediaSourceObject();
    if (ms) {
      *contains = true;
    }
  }
}

bool
nsDocument::ContainsMSEContent()
{
  bool containsMSE = false;
  EnumerateActivityObservers(CheckIfContainsMSEContent,
                             static_cast<void*>(&containsMSE));
  return containsMSE;
}

static void
NotifyActivityChanged(nsISupports *aSupports, void *aUnused)
{
  nsCOMPtr<nsIDOMHTMLMediaElement> domMediaElem(do_QueryInterface(aSupports));
  if (domMediaElem) {
    nsCOMPtr<nsIContent> content(do_QueryInterface(domMediaElem));
    MOZ_ASSERT(content, "aSupports is not a content");
    HTMLMediaElement* mediaElem = static_cast<HTMLMediaElement*>(content.get());
    mediaElem->NotifyOwnerDocumentActivityChanged();
  }
  nsCOMPtr<nsIObjectLoadingContent> objectLoadingContent(do_QueryInterface(aSupports));
  if (objectLoadingContent) {
    nsObjectLoadingContent* olc = static_cast<nsObjectLoadingContent*>(objectLoadingContent.get());
    olc->NotifyOwnerDocumentActivityChanged();
  }
  nsCOMPtr<nsIDocumentActivity> objectDocumentActivity(do_QueryInterface(aSupports));
  if (objectDocumentActivity) {
    objectDocumentActivity->NotifyOwnerDocumentActivityChanged();
  }
}

void
nsIDocument::SetContainer(nsDocShell* aContainer)
{
  if (aContainer) {
    mDocumentContainer = aContainer;
  } else {
    mDocumentContainer = WeakPtr<nsDocShell>();
  }

  EnumerateActivityObservers(NotifyActivityChanged, nullptr);
  if (!aContainer) {
    return;
  }

  // Get the Docshell
  if (aContainer->ItemType() == nsIDocShellTreeItem::typeContent) {
    // check if same type root
    nsCOMPtr<nsIDocShellTreeItem> sameTypeRoot;
    aContainer->GetSameTypeRootTreeItem(getter_AddRefs(sameTypeRoot));
    NS_ASSERTION(sameTypeRoot, "No document shell root tree item from document shell tree item!");

    if (sameTypeRoot == aContainer) {
      static_cast<nsDocument*>(this)->SetIsTopLevelContentDocument(true);
    }

    static_cast<nsDocument*>(this)->SetIsContentDocument(true);
  }
}

nsISupports*
nsIDocument::GetContainer() const
{
  return static_cast<nsIDocShell*>(mDocumentContainer);
}

void
nsDocument::SetScriptGlobalObject(nsIScriptGlobalObject *aScriptGlobalObject)
{
#ifdef DEBUG
  {
    nsCOMPtr<nsPIDOMWindow> win(do_QueryInterface(aScriptGlobalObject));

    NS_ASSERTION(!win || win->IsInnerWindow(),
                 "Script global object must be an inner window!");
  }
#endif
  MOZ_ASSERT(aScriptGlobalObject || !mAnimationController ||
             mAnimationController->IsPausedByType(
               nsSMILTimeContainer::PAUSE_PAGEHIDE |
               nsSMILTimeContainer::PAUSE_BEGIN),
             "Clearing window pointer while animations are unpaused");

  if (mScriptGlobalObject && !aScriptGlobalObject) {
    // We're detaching from the window.  We need to grab a pointer to
    // our layout history state now.
    mLayoutHistoryState = GetLayoutHistoryState();

    if (mPresShell && !EventHandlingSuppressed() && !AnimationsPaused()) {
      RevokeAnimationFrameNotifications();
    }

    // Also make sure to remove our onload blocker now if we haven't done it yet
    if (mOnloadBlockCount != 0) {
      nsCOMPtr<nsILoadGroup> loadGroup = GetDocumentLoadGroup();
      if (loadGroup) {
        loadGroup->RemoveRequest(mOnloadBlocker, nullptr, NS_OK);
      }
    }

    using mozilla::dom::workers::ServiceWorkerManager;
    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    if (swm) {
      ErrorResult error;
      if (swm->IsControlled(this, error)) {
        imgLoader* loader = nsContentUtils::GetImgLoaderForDocument(this);
        if (loader) {
          loader->ClearCacheForControlledDocument(this);
        }

        // We may become controlled again if this document comes back out
        // of bfcache.  Clear our state to allow that to happen.  Only
        // clear this flag if we are actually controlled, though, so pages
        // that were force reloaded don't become controlled when they
        // come out of bfcache.
        mMaybeServiceWorkerControlled = false;
      }
      swm->MaybeStopControlling(this);
    }

    // Remove ourself from the list of clients.  We only register
    // content principal documents in this list.
    if (!nsContentUtils::IsSystemPrincipal(GetPrincipal()) &&
        !GetPrincipal()->GetIsNullPrincipal()) {
      nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
      if (os) {
        os->RemoveObserver(this, "service-worker-get-client");
      }
    }

  } else if (!mScriptGlobalObject && aScriptGlobalObject &&
             mDocumentContainer && GetChannel() &&
             !nsContentUtils::IsSystemPrincipal(GetPrincipal()) &&
             !GetPrincipal()->GetIsNullPrincipal()) {
    // This document is being activated.  Register it in the list of
    // clients.  We only do this for content principal documents
    // since we can never observe system or null principals.
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os) {
      os->AddObserver(this, "service-worker-get-client", /* ownsWeak */ false);
    }
  }

  // BlockOnload() might be called before mScriptGlobalObject is set.
  // We may need to add the blocker once mScriptGlobalObject is set.
  bool needOnloadBlocker = !mScriptGlobalObject && aScriptGlobalObject;

  mScriptGlobalObject = aScriptGlobalObject;

  if (needOnloadBlocker) {
    EnsureOnloadBlocker();
  }

  if (aScriptGlobalObject) {
    mHasHadScriptHandlingObject = true;
    mHasHadDefaultView = true;
    // Go back to using the docshell for the layout history state
    mLayoutHistoryState = nullptr;
    mScopeObject = do_GetWeakReference(aScriptGlobalObject);
#ifdef DEBUG
    if (!mWillReparent) {
      // We really shouldn't have a wrapper here but if we do we need to make sure
      // it has the correct parent.
      JSObject *obj = GetWrapperPreserveColor();
      if (obj) {
        JSObject *newScope = aScriptGlobalObject->GetGlobalJSObject();
        NS_ASSERTION(js::GetGlobalForObjectCrossCompartment(obj) == newScope,
                     "Wrong scope, this is really bad!");
      }
    }
#endif

    if (mAllowDNSPrefetch) {
      nsCOMPtr<nsIDocShell> docShell(mDocumentContainer);
      if (docShell) {
#ifdef DEBUG
        nsCOMPtr<nsIWebNavigation> webNav =
          do_GetInterface(aScriptGlobalObject);
        NS_ASSERTION(SameCOMIdentity(webNav, docShell),
                     "Unexpected container or script global?");
#endif
        bool allowDNSPrefetch;
        docShell->GetAllowDNSPrefetch(&allowDNSPrefetch);
        mAllowDNSPrefetch = allowDNSPrefetch;
      }
    }

    MaybeRescheduleAnimationFrameNotifications();
    mRegistry = new Registry();
  }

  // Remember the pointer to our window (or lack there of), to avoid
  // having to QI every time it's asked for.
  nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(mScriptGlobalObject);
  mWindow = window;

  // Now that we know what our window is, we can flush the CSP errors to the
  // Web Console. We are flushing all messages that occured and were stored
  // in the queue prior to this point.
  nsCOMPtr<nsIContentSecurityPolicy> csp;
  NodePrincipal()->GetCsp(getter_AddRefs(csp));
  if (csp) {
    static_cast<nsCSPContext*>(csp.get())->flushConsoleMessages();
  }

  nsCOMPtr<nsIHttpChannelInternal> internalChannel =
    do_QueryInterface(GetChannel());
  if (internalChannel) {
    nsCOMArray<nsISecurityConsoleMessage> messages;
    internalChannel->TakeAllSecurityMessages(messages);
    SendToConsole(messages);
  }

  // Set our visibility state, but do not fire the event.  This is correct
  // because either we're coming out of bfcache (in which case IsVisible() will
  // still test false at this point and no state change will happen) or we're
  // doing the initial document load and don't want to fire the event for this
  // change.
  dom::VisibilityState oldState = mVisibilityState;
  mVisibilityState = GetVisibilityState();
  // When the visibility is changed, notify it to observers.
  // Some observers need the notification, for example HTMLMediaElement uses
  // it to update internal media resource allocation.
  // When video is loaded via VideoDocument, HTMLMediaElement and MediaDecoder
  // creation are already done before nsDocument::SetScriptGlobalObject() call.
  // MediaDecoder decides whether starting decoding is decided based on
  // document's visibility. When the MediaDecoder is created,
  // nsDocument::SetScriptGlobalObject() is not yet called and document is
  // hidden state. Therefore the MediaDecoder decides that decoding is
  // not yet necessary. But soon after nsDocument::SetScriptGlobalObject()
  // call, the document becomes not hidden. At the time, MediaDecoder needs
  // to know it and needs to start updating decoding.
  if (oldState != mVisibilityState) {
    EnumerateActivityObservers(NotifyActivityChanged, nullptr);
  }

  // The global in the template contents owner document should be the same.
  if (mTemplateContentsOwner && mTemplateContentsOwner != this) {
    mTemplateContentsOwner->SetScriptGlobalObject(aScriptGlobalObject);
  }

  if (!mMaybeServiceWorkerControlled && mDocumentContainer && mScriptGlobalObject && GetChannel()) {
    nsCOMPtr<nsIDocShell> docShell(mDocumentContainer);
    uint32_t loadType;
    docShell->GetLoadType(&loadType);

    // If we are shift-reloaded, don't associate with a ServiceWorker.
    if (IsForceReloadType(loadType)) {
      NS_WARNING("Page was shift reloaded, skipping ServiceWorker control");
      return;
    }

    nsCOMPtr<nsIServiceWorkerManager> swm = mozilla::services::GetServiceWorkerManager();
    if (swm) {
      // If this document is being resurrected from the bfcache, then we may
      // already have a document ID.  In that case reuse the same ID.  Otherwise
      // get our document ID from the docshell.
      nsString documentId(GetId());
      if (documentId.IsEmpty()) {
        static_cast<nsDocShell*>(docShell.get())->GetInterceptedDocumentId(documentId);
      }

      swm->MaybeStartControlling(this, documentId);
      mMaybeServiceWorkerControlled = true;
    }
  }
}

nsIScriptGlobalObject*
nsDocument::GetScriptHandlingObjectInternal() const
{
  MOZ_ASSERT(!mScriptGlobalObject,
             "Do not call this when mScriptGlobalObject is set!");
  if (mHasHadDefaultView) {
    return nullptr;
  }

  nsCOMPtr<nsIScriptGlobalObject> scriptHandlingObject =
    do_QueryReferent(mScopeObject);
  nsCOMPtr<nsPIDOMWindow> win = do_QueryInterface(scriptHandlingObject);
  if (win) {
    NS_ASSERTION(win->IsInnerWindow(), "Should have inner window here!");
    nsPIDOMWindow* outer = win->GetOuterWindow();
    if (!outer || outer->GetCurrentInnerWindow() != win) {
      NS_WARNING("Wrong inner/outer window combination!");
      return nullptr;
    }
  }
  return scriptHandlingObject;
}
void
nsDocument::SetScriptHandlingObject(nsIScriptGlobalObject* aScriptObject)
{
  NS_ASSERTION(!mScriptGlobalObject ||
               mScriptGlobalObject == aScriptObject,
               "Wrong script object!");
  nsCOMPtr<nsPIDOMWindow> win = do_QueryInterface(aScriptObject);
  NS_ASSERTION(!win || win->IsInnerWindow(), "Should have inner window here!");
  if (aScriptObject) {
    mScopeObject = do_GetWeakReference(aScriptObject);
    mHasHadScriptHandlingObject = true;
    mHasHadDefaultView = false;
  }
}

bool
nsDocument::IsTopLevelContentDocument()
{
  return mIsTopLevelContentDocument;
}

void
nsDocument::SetIsTopLevelContentDocument(bool aIsTopLevelContentDocument)
{
  mIsTopLevelContentDocument = aIsTopLevelContentDocument;
}

bool
nsDocument::IsContentDocument() const
{
  return mIsContentDocument;
}

void
nsDocument::SetIsContentDocument(bool aIsContentDocument)
{
  mIsContentDocument = aIsContentDocument;
}

nsPIDOMWindow *
nsDocument::GetWindowInternal() const
{
  MOZ_ASSERT(!mWindow, "This should not be called when mWindow is not null!");
  // Let's use mScriptGlobalObject. Even if the document is already removed from
  // the docshell, the outer window might be still obtainable from the it.
  nsCOMPtr<nsPIDOMWindow> win;
  if (mRemovedFromDocShell) {
    // The docshell returns the outer window we are done.
    nsCOMPtr<nsIDocShell> kungfuDeathGrip(mDocumentContainer);
    if (mDocumentContainer) {
      win = mDocumentContainer->GetWindow();
    }
  } else {
    win = do_QueryInterface(mScriptGlobalObject);
    if (win) {
      // mScriptGlobalObject is always the inner window, let's get the outer.
      win = win->GetOuterWindow();
    }
  }

  return win;
}

nsScriptLoader*
nsDocument::ScriptLoader()
{
  return mScriptLoader;
}

bool
nsDocument::InternalAllowXULXBL()
{
  if (nsContentUtils::AllowXULXBLForPrincipal(NodePrincipal())) {
    mAllowXULXBL = eTriTrue;
    return true;
  }

  mAllowXULXBL = eTriFalse;
  return false;
}

// Note: We don't hold a reference to the document observer; we assume
// that it has a live reference to the document.
void
nsDocument::AddObserver(nsIDocumentObserver* aObserver)
{
  NS_ASSERTION(mObservers.IndexOf(aObserver) == nsTArray<int>::NoIndex,
               "Observer already in the list");
  mObservers.AppendElement(aObserver);
  AddMutationObserver(aObserver);
}

bool
nsDocument::RemoveObserver(nsIDocumentObserver* aObserver)
{
  // If we're in the process of destroying the document (and we're
  // informing the observers of the destruction), don't remove the
  // observers from the list. This is not a big deal, since we
  // don't hold a live reference to the observers.
  if (!mInDestructor) {
    RemoveMutationObserver(aObserver);
    return mObservers.RemoveElement(aObserver);
  }

  return mObservers.Contains(aObserver);
}

void
nsDocument::MaybeEndOutermostXBLUpdate()
{
  // Only call BindingManager()->EndOutermostUpdate() when
  // we're not in an update and it is safe to run scripts.
  if (mUpdateNestLevel == 0 && mInXBLUpdate) {
    if (nsContentUtils::IsSafeToRunScript()) {
      mInXBLUpdate = false;
      BindingManager()->EndOutermostUpdate();
    } else if (!mInDestructor) {
      nsContentUtils::AddScriptRunner(
        NewRunnableMethod(this, &nsDocument::MaybeEndOutermostXBLUpdate));
    }
  }
}

void
nsDocument::BeginUpdate(nsUpdateType aUpdateType)
{
  if (mUpdateNestLevel == 0 && !mInXBLUpdate) {
    mInXBLUpdate = true;
    BindingManager()->BeginOutermostUpdate();
  }

  ++mUpdateNestLevel;
  nsContentUtils::AddScriptBlocker();
  NS_DOCUMENT_NOTIFY_OBSERVERS(BeginUpdate, (this, aUpdateType));
}

void
nsDocument::EndUpdate(nsUpdateType aUpdateType)
{
  NS_DOCUMENT_NOTIFY_OBSERVERS(EndUpdate, (this, aUpdateType));

  nsContentUtils::RemoveScriptBlocker();

  --mUpdateNestLevel;

  // This set of updates may have created XBL bindings.  Let the
  // binding manager know we're done.
  MaybeEndOutermostXBLUpdate();

  MaybeInitializeFinalizeFrameLoaders();
}

void
nsDocument::BeginLoad()
{
  // Block onload here to prevent having to deal with blocking and
  // unblocking it while we know the document is loading.
  BlockOnload();
  mDidFireDOMContentLoaded = false;
  BlockDOMContentLoaded();

  if (mScriptLoader) {
    mScriptLoader->BeginDeferringScripts();
  }

  NS_DOCUMENT_NOTIFY_OBSERVERS(BeginLoad, (this));
}

void
nsDocument::ReportEmptyGetElementByIdArg()
{
  nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                  NS_LITERAL_CSTRING("DOM"), this,
                                  nsContentUtils::eDOM_PROPERTIES,
                                  "EmptyGetElementByIdParam");
}

Element*
nsDocument::GetElementById(const nsAString& aElementId)
{
  if (!CheckGetElementByIdArg(aElementId)) {
    return nullptr;
  }

  nsIdentifierMapEntry *entry = mIdentifierMap.GetEntry(aElementId);
  return entry ? entry->GetIdElement() : nullptr;
}

const nsTArray<Element*>*
nsDocument::GetAllElementsForId(const nsAString& aElementId) const
{
  if (aElementId.IsEmpty()) {
    return nullptr;
  }

  nsIdentifierMapEntry *entry = mIdentifierMap.GetEntry(aElementId);
  return entry ? &entry->GetIdElements() : nullptr;
}

NS_IMETHODIMP
nsDocument::GetElementById(const nsAString& aId, nsIDOMElement** aReturn)
{
  Element *content = GetElementById(aId);
  if (content) {
    return CallQueryInterface(content, aReturn);
  }

  *aReturn = nullptr;

  return NS_OK;
}

Element*
nsDocument::AddIDTargetObserver(nsIAtom* aID, IDTargetObserver aObserver,
                                void* aData, bool aForImage)
{
  nsDependentAtomString id(aID);

  if (!CheckGetElementByIdArg(id))
    return nullptr;

  nsIdentifierMapEntry *entry = mIdentifierMap.PutEntry(id);
  NS_ENSURE_TRUE(entry, nullptr);

  entry->AddContentChangeCallback(aObserver, aData, aForImage);
  return aForImage ? entry->GetImageIdElement() : entry->GetIdElement();
}

void
nsDocument::RemoveIDTargetObserver(nsIAtom* aID, IDTargetObserver aObserver,
                                   void* aData, bool aForImage)
{
  nsDependentAtomString id(aID);

  if (!CheckGetElementByIdArg(id))
    return;

  nsIdentifierMapEntry *entry = mIdentifierMap.GetEntry(id);
  if (!entry) {
    return;
  }

  entry->RemoveContentChangeCallback(aObserver, aData, aForImage);
}

NS_IMETHODIMP
nsDocument::MozSetImageElement(const nsAString& aImageElementId,
                               nsIDOMElement* aImageElement)
{
  nsCOMPtr<Element> el = do_QueryInterface(aImageElement);
  MozSetImageElement(aImageElementId, el);
  return NS_OK;
}

void
nsDocument::MozSetImageElement(const nsAString& aImageElementId,
                               Element* aElement)
{
  if (aImageElementId.IsEmpty())
    return;

  // Hold a script blocker while calling SetImageElement since that can call
  // out to id-observers
  nsAutoScriptBlocker scriptBlocker;

  nsIdentifierMapEntry *entry = mIdentifierMap.PutEntry(aImageElementId);
  if (entry) {
    entry->SetImageElement(aElement);
    if (entry->IsEmpty()) {
      mIdentifierMap.RemoveEntry(entry);
    }
  }
}

Element*
nsDocument::LookupImageElement(const nsAString& aId)
{
  if (aId.IsEmpty())
    return nullptr;

  nsIdentifierMapEntry *entry = mIdentifierMap.GetEntry(aId);
  return entry ? entry->GetImageIdElement() : nullptr;
}

void
nsDocument::DispatchContentLoadedEvents()
{
  // If you add early returns from this method, make sure you're
  // calling UnblockOnload properly.

  // Unpin references to preloaded images
  mPreloadingImages.Clear();

  // DOM manipulation after content loaded should not care if the element
  // came from the preloader.
  mPreloadedPreconnects.Clear();

  if (mTiming) {
    mTiming->NotifyDOMContentLoadedStart(nsIDocument::GetDocumentURI());
  }

  // Dispatch observer notification to notify observers document is interactive.
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  nsIPrincipal *principal = GetPrincipal();
  os->NotifyObservers(static_cast<nsIDocument*>(this),
                      nsContentUtils::IsSystemPrincipal(principal) ?
                        "chrome-document-interactive" :
                        "content-document-interactive",
                      nullptr);

  // Fire a DOM event notifying listeners that this document has been
  // loaded (excluding images and other loads initiated by this
  // document).
  nsContentUtils::DispatchTrustedEvent(this, static_cast<nsIDocument*>(this),
                                       NS_LITERAL_STRING("DOMContentLoaded"),
                                       true, false);

  RefPtr<TimelineConsumers> timelines = TimelineConsumers::Get();
  nsIDocShell* docShell = this->GetDocShell();

  if (timelines && timelines->HasConsumer(docShell)) {
    timelines->AddMarkerForDocShell(
      docShell, "document::DOMContentLoaded", MarkerTracingType::TIMESTAMP);
  }

  if (mTiming) {
    mTiming->NotifyDOMContentLoadedEnd(nsIDocument::GetDocumentURI());
  }

  // If this document is a [i]frame, fire a DOMFrameContentLoaded
  // event on all parent documents notifying that the HTML (excluding
  // other external files such as images and stylesheets) in a frame
  // has finished loading.

  // target_frame is the [i]frame element that will be used as the
  // target for the event. It's the [i]frame whose content is done
  // loading.
  nsCOMPtr<EventTarget> target_frame;

  if (mParentDocument) {
    target_frame = mParentDocument->FindContentForSubDocument(this);
  }

  if (target_frame) {
    nsCOMPtr<nsIDocument> parent = mParentDocument;
    do {
      nsCOMPtr<nsIDOMDocument> domDoc = do_QueryInterface(parent);

      nsCOMPtr<nsIDOMEvent> event;
      if (domDoc) {
        domDoc->CreateEvent(NS_LITERAL_STRING("Events"),
                            getter_AddRefs(event));

      }

      if (event) {
        event->InitEvent(NS_LITERAL_STRING("DOMFrameContentLoaded"), true,
                         true);

        event->SetTarget(target_frame);
        event->SetTrusted(true);

        // To dispatch this event we must manually call
        // EventDispatcher::Dispatch() on the ancestor document since the
        // target is not in the same document, so the event would never reach
        // the ancestor document if we used the normal event
        // dispatching code.

        WidgetEvent* innerEvent = event->WidgetEventPtr();
        if (innerEvent) {
          nsEventStatus status = nsEventStatus_eIgnore;

          nsIPresShell *shell = parent->GetShell();
          if (shell) {
            RefPtr<nsPresContext> context = shell->GetPresContext();

            if (context) {
              EventDispatcher::Dispatch(parent, context, innerEvent, event,
                                        &status);
            }
          }
        }
      }

      parent = parent->GetParentDocument();
    } while (parent);
  }

  // If the document has a manifest attribute, fire a MozApplicationManifest
  // event.
  Element* root = GetRootElement();
  if (root && root->HasAttr(kNameSpaceID_None, nsGkAtoms::manifest)) {
    nsContentUtils::DispatchChromeEvent(this, static_cast<nsIDocument*>(this),
                                        NS_LITERAL_STRING("MozApplicationManifest"),
                                        true, true);
  }

  if (mMaybeServiceWorkerControlled) {
    using mozilla::dom::workers::ServiceWorkerManager;
    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    if (swm) {
      swm->MaybeCheckNavigationUpdate(this);
    }
  }

  UnblockOnload(true);
}

void
nsDocument::EndLoad()
{
  // Drop the ref to our parser, if any, but keep hold of the sink so that we
  // can flush it from FlushPendingNotifications as needed.  We might have to
  // do that to get a StartLayout() to happen.
  if (mParser) {
    mWeakSink = do_GetWeakReference(mParser->GetContentSink());
    mParser = nullptr;
  }

  NS_DOCUMENT_NOTIFY_OBSERVERS(EndLoad, (this));

  UnblockDOMContentLoaded();
}

void
nsDocument::UnblockDOMContentLoaded()
{
  MOZ_ASSERT(mBlockDOMContentLoaded);
  if (--mBlockDOMContentLoaded != 0 || mDidFireDOMContentLoaded) {
    return;
  }
  mDidFireDOMContentLoaded = true;

  MOZ_ASSERT(mReadyState == READYSTATE_INTERACTIVE);
  if (!mSynchronousDOMContentLoaded) {
    nsCOMPtr<nsIRunnable> ev =
      NewRunnableMethod(this, &nsDocument::DispatchContentLoadedEvents);
    NS_DispatchToCurrentThread(ev);
  } else {
    DispatchContentLoadedEvents();
  }
}

void
nsDocument::ContentStateChanged(nsIContent* aContent, EventStates aStateMask)
{
  NS_PRECONDITION(!nsContentUtils::IsSafeToRunScript(),
                  "Someone forgot a scriptblocker");
  NS_DOCUMENT_NOTIFY_OBSERVERS(ContentStateChanged,
                               (this, aContent, aStateMask));
}

void
nsDocument::DocumentStatesChanged(EventStates aStateMask)
{
  // Invalidate our cached state.
  mGotDocumentState &= ~aStateMask;
  mDocumentState &= ~aStateMask;

  NS_DOCUMENT_NOTIFY_OBSERVERS(DocumentStatesChanged, (this, aStateMask));
}

void
nsDocument::StyleRuleChanged(StyleSheetHandle aSheet,
                             css::Rule* aStyleRule)
{
  NS_DOCUMENT_NOTIFY_OBSERVERS(StyleRuleChanged, (aSheet));

  if (StyleSheetChangeEventsEnabled()) {
    DO_STYLESHEET_NOTIFICATION(StyleRuleChangeEvent,
                               "StyleRuleChanged",
                               mRule,
                               aStyleRule ? aStyleRule->GetDOMRule() : nullptr);
  }
}

void
nsDocument::StyleRuleAdded(StyleSheetHandle aSheet,
                           css::Rule* aStyleRule)
{
  NS_DOCUMENT_NOTIFY_OBSERVERS(StyleRuleAdded, (aSheet));

  if (StyleSheetChangeEventsEnabled()) {
    DO_STYLESHEET_NOTIFICATION(StyleRuleChangeEvent,
                               "StyleRuleAdded",
                               mRule,
                               aStyleRule ? aStyleRule->GetDOMRule()
                                          : nullptr);
  }
}

void
nsDocument::StyleRuleRemoved(StyleSheetHandle aSheet,
                             css::Rule* aStyleRule)
{
  NS_DOCUMENT_NOTIFY_OBSERVERS(StyleRuleRemoved, (aSheet));

  if (StyleSheetChangeEventsEnabled()) {
    DO_STYLESHEET_NOTIFICATION(StyleRuleChangeEvent,
                               "StyleRuleRemoved",
                               mRule,
                               aStyleRule ? aStyleRule->GetDOMRule()
                                          : nullptr);
  }
}

#undef DO_STYLESHEET_NOTIFICATION

already_AddRefed<AnonymousContent>
nsIDocument::InsertAnonymousContent(Element& aElement, ErrorResult& aRv)
{
  nsIPresShell* shell = GetShell();
  if (!shell || !shell->GetCanvasFrame()) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  nsAutoScriptBlocker scriptBlocker;
  nsCOMPtr<Element> container = shell->GetCanvasFrame()
                                     ->GetCustomContentContainer();
  if (!container) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  // Clone the node to avoid returning a direct reference
  nsCOMPtr<nsINode> clonedElement = aElement.CloneNode(true, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Insert the element into the container
  nsresult rv;
  rv = container->AppendChildTo(clonedElement->AsContent(), true);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  RefPtr<AnonymousContent> anonymousContent =
    new AnonymousContent(clonedElement->AsElement());
  mAnonymousContents.AppendElement(anonymousContent);

  shell->GetCanvasFrame()->ShowCustomContentContainer();

  return anonymousContent.forget();
}

void
nsIDocument::RemoveAnonymousContent(AnonymousContent& aContent,
                                    ErrorResult& aRv)
{
  nsIPresShell* shell = GetShell();
  if (!shell || !shell->GetCanvasFrame()) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  nsAutoScriptBlocker scriptBlocker;
  nsCOMPtr<Element> container = shell->GetCanvasFrame()
                                     ->GetCustomContentContainer();
  if (!container) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  // Iterate over mAnonymousContents to find and remove the given node.
  for (size_t i = 0, len = mAnonymousContents.Length(); i < len; ++i) {
    if (mAnonymousContents[i] == &aContent) {
      // Get the node from the customContent
      nsCOMPtr<Element> node = aContent.GetContentNode();

      // Remove the entry in mAnonymousContents
      mAnonymousContents.RemoveElementAt(i);

      // Remove the node from its container
      container->RemoveChild(*node, aRv);
      if (aRv.Failed()) {
        return;
      }

      break;
    }
  }
  if (mAnonymousContents.IsEmpty()) {
    shell->GetCanvasFrame()->HideCustomContentContainer();
  }
}

//
// nsIDOMDocument interface
//
DocumentType*
nsIDocument::GetDoctype() const
{
  for (nsIContent* child = GetFirstChild();
       child;
       child = child->GetNextSibling()) {
    if (child->NodeType() == nsIDOMNode::DOCUMENT_TYPE_NODE) {
      return static_cast<DocumentType*>(child);
    }
  }
  return nullptr;
}

NS_IMETHODIMP
nsDocument::GetDoctype(nsIDOMDocumentType** aDoctype)
{
  MOZ_ASSERT(aDoctype);
  nsCOMPtr<nsIDOMDocumentType> doctype = nsIDocument::GetDoctype();
  doctype.forget(aDoctype);
  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetImplementation(nsIDOMDOMImplementation** aImplementation)
{
  ErrorResult rv;
  *aImplementation = GetImplementation(rv);
  if (rv.Failed()) {
    MOZ_ASSERT(!*aImplementation);
    return rv.StealNSResult();
  }
  NS_ADDREF(*aImplementation);
  return NS_OK;
}

DOMImplementation*
nsDocument::GetImplementation(ErrorResult& rv)
{
  if (!mDOMImplementation) {
    nsCOMPtr<nsIURI> uri;
    NS_NewURI(getter_AddRefs(uri), "about:blank");
    if (!uri) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return nullptr;
    }
    bool hasHadScriptObject = true;
    nsIScriptGlobalObject* scriptObject =
      GetScriptHandlingObject(hasHadScriptObject);
    if (!scriptObject && hasHadScriptObject) {
      rv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }
    mDOMImplementation = new DOMImplementation(this,
      scriptObject ? scriptObject : GetScopeObject(), uri, uri);
  }

  return mDOMImplementation;
}

NS_IMETHODIMP
nsDocument::GetDocumentElement(nsIDOMElement** aDocumentElement)
{
  NS_ENSURE_ARG_POINTER(aDocumentElement);

  Element* root = GetRootElement();
  if (root) {
    return CallQueryInterface(root, aDocumentElement);
  }

  *aDocumentElement = nullptr;

  return NS_OK;
}

NS_IMETHODIMP
nsDocument::CreateElement(const nsAString& aTagName,
                          nsIDOMElement** aReturn)
{
  *aReturn = nullptr;
  ErrorResult rv;
  nsCOMPtr<Element> element = nsIDocument::CreateElement(aTagName, rv);
  NS_ENSURE_FALSE(rv.Failed(), rv.StealNSResult());
  return CallQueryInterface(element, aReturn);
}

bool IsLowercaseASCII(const nsAString& aValue)
{
  int32_t len = aValue.Length();
  for (int32_t i = 0; i < len; ++i) {
    char16_t c = aValue[i];
    if (!(0x0061 <= (c) && ((c) <= 0x007a))) {
      return false;
    }
  }
  return true;
}

already_AddRefed<Element>
nsIDocument::CreateElement(const nsAString& aTagName, ErrorResult& rv)
{
  rv = nsContentUtils::CheckQName(aTagName, false);
  if (rv.Failed()) {
    return nullptr;
  }

  bool needsLowercase = IsHTMLDocument() && !IsLowercaseASCII(aTagName);
  nsAutoString lcTagName;
  if (needsLowercase) {
    nsContentUtils::ASCIIToLower(aTagName, lcTagName);
  }

  return CreateElem(needsLowercase ? lcTagName : aTagName, nullptr,
                    mDefaultElementType);
}

void
nsDocument::SetupCustomElement(Element* aElement,
                               uint32_t aNamespaceID,
                               const nsAString* aTypeExtension)
{
  if (!mRegistry) {
    return;
  }

  nsCOMPtr<nsIAtom> tagAtom = aElement->NodeInfo()->NameAtom();
  nsCOMPtr<nsIAtom> typeAtom = aTypeExtension ?
    NS_Atomize(*aTypeExtension) : tagAtom;

  if (aTypeExtension && !aElement->HasAttr(kNameSpaceID_None, nsGkAtoms::is)) {
    // Custom element setup in the parser happens after the "is"
    // attribute is added.
    aElement->SetAttr(kNameSpaceID_None, nsGkAtoms::is, *aTypeExtension, true);
  }

  CustomElementDefinition* data;
  CustomElementHashKey key(aNamespaceID, typeAtom);
  if (!mRegistry->mCustomDefinitions.Get(&key, &data)) {
    // The type extension doesn't exist in the registry,
    // thus we don't need to enqueue callback or adjust
    // the "is" attribute, but it is possibly an upgrade candidate.
    RegisterUnresolvedElement(aElement, typeAtom);
    return;
  }

  if (data->mLocalName != tagAtom) {
    // The element doesn't match the local name for the
    // definition, thus the element isn't a custom element
    // and we don't need to do anything more.
    return;
  }

  // Enqueuing the created callback will set the CustomElementData on the
  // element, causing prototype swizzling to occur in Element::WrapObject.
  EnqueueLifecycleCallback(nsIDocument::eCreated, aElement, nullptr, data);
}

already_AddRefed<Element>
nsDocument::CreateElement(const nsAString& aTagName,
                          const nsAString& aTypeExtension,
                          ErrorResult& rv)
{
  RefPtr<Element> elem = nsIDocument::CreateElement(aTagName, rv);
  if (rv.Failed()) {
    return nullptr;
  }

  if (!aTagName.Equals(aTypeExtension)) {
    // Custom element type can not extend itself.
    SetupCustomElement(elem, GetDefaultNamespaceID(), &aTypeExtension);
  }

  return elem.forget();
}

NS_IMETHODIMP
nsDocument::CreateElementNS(const nsAString& aNamespaceURI,
                            const nsAString& aQualifiedName,
                            nsIDOMElement** aReturn)
{
  *aReturn = nullptr;
  ErrorResult rv;
  nsCOMPtr<Element> element =
    nsIDocument::CreateElementNS(aNamespaceURI, aQualifiedName, rv);
  NS_ENSURE_FALSE(rv.Failed(), rv.StealNSResult());
  return CallQueryInterface(element, aReturn);
}

already_AddRefed<Element>
nsIDocument::CreateElementNS(const nsAString& aNamespaceURI,
                             const nsAString& aQualifiedName,
                             ErrorResult& rv)
{
  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  rv = nsContentUtils::GetNodeInfoFromQName(aNamespaceURI,
                                            aQualifiedName,
                                            mNodeInfoManager,
                                            nsIDOMNode::ELEMENT_NODE,
                                            getter_AddRefs(nodeInfo));
  if (rv.Failed()) {
    return nullptr;
  }

  nsCOMPtr<Element> element;
  rv = NS_NewElement(getter_AddRefs(element), nodeInfo.forget(),
                     NOT_FROM_PARSER);
  if (rv.Failed()) {
    return nullptr;
  }
  return element.forget();
}

already_AddRefed<Element>
nsDocument::CreateElementNS(const nsAString& aNamespaceURI,
                            const nsAString& aQualifiedName,
                            const nsAString& aTypeExtension,
                            ErrorResult& rv)
{
  RefPtr<Element> elem = nsIDocument::CreateElementNS(aNamespaceURI,
                                                        aQualifiedName,
                                                        rv);
  if (rv.Failed()) {
    return nullptr;
  }

  int32_t nameSpaceId = kNameSpaceID_Wildcard;
  if (!aNamespaceURI.EqualsLiteral("*")) {
    rv = nsContentUtils::NameSpaceManager()->RegisterNameSpace(aNamespaceURI,
                                                               nameSpaceId);
    if (rv.Failed()) {
      return nullptr;
    }
  }

  if (!aQualifiedName.Equals(aTypeExtension)) {
    // A custom element type can not extend itself.
    SetupCustomElement(elem, nameSpaceId, &aTypeExtension);
  }

  return elem.forget();
}

NS_IMETHODIMP
nsDocument::CreateTextNode(const nsAString& aData, nsIDOMText** aReturn)
{
  *aReturn = nsIDocument::CreateTextNode(aData).take();
  return NS_OK;
}

already_AddRefed<nsTextNode>
nsIDocument::CreateTextNode(const nsAString& aData) const
{
  RefPtr<nsTextNode> text = new nsTextNode(mNodeInfoManager);
  // Don't notify; this node is still being created.
  text->SetText(aData, false);
  return text.forget();
}

NS_IMETHODIMP
nsDocument::CreateDocumentFragment(nsIDOMDocumentFragment** aReturn)
{
  *aReturn = nsIDocument::CreateDocumentFragment().take();
  return NS_OK;
}

already_AddRefed<DocumentFragment>
nsIDocument::CreateDocumentFragment() const
{
  RefPtr<DocumentFragment> frag = new DocumentFragment(mNodeInfoManager);
  return frag.forget();
}

NS_IMETHODIMP
nsDocument::CreateComment(const nsAString& aData, nsIDOMComment** aReturn)
{
  *aReturn = nsIDocument::CreateComment(aData).take();
  return NS_OK;
}

// Unfortunately, bareword "Comment" is ambiguous with some Mac system headers.
already_AddRefed<dom::Comment>
nsIDocument::CreateComment(const nsAString& aData) const
{
  RefPtr<dom::Comment> comment = new dom::Comment(mNodeInfoManager);

  // Don't notify; this node is still being created.
  comment->SetText(aData, false);
  return comment.forget();
}

NS_IMETHODIMP
nsDocument::CreateCDATASection(const nsAString& aData,
                               nsIDOMCDATASection** aReturn)
{
  NS_ENSURE_ARG_POINTER(aReturn);
  ErrorResult rv;
  *aReturn = nsIDocument::CreateCDATASection(aData, rv).take();
  return rv.StealNSResult();
}

already_AddRefed<CDATASection>
nsIDocument::CreateCDATASection(const nsAString& aData,
                                ErrorResult& rv)
{
  if (IsHTMLDocument()) {
    rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return nullptr;
  }

  if (FindInReadable(NS_LITERAL_STRING("]]>"), aData)) {
    rv.Throw(NS_ERROR_DOM_INVALID_CHARACTER_ERR);
    return nullptr;
  }

  RefPtr<CDATASection> cdata = new CDATASection(mNodeInfoManager);

  // Don't notify; this node is still being created.
  cdata->SetText(aData, false);

  return cdata.forget();
}

NS_IMETHODIMP
nsDocument::CreateProcessingInstruction(const nsAString& aTarget,
                                        const nsAString& aData,
                                        nsIDOMProcessingInstruction** aReturn)
{
  ErrorResult rv;
  *aReturn =
    nsIDocument::CreateProcessingInstruction(aTarget, aData, rv).take();
  return rv.StealNSResult();
}

already_AddRefed<ProcessingInstruction>
nsIDocument::CreateProcessingInstruction(const nsAString& aTarget,
                                         const nsAString& aData,
                                         ErrorResult& rv) const
{
  nsresult res = nsContentUtils::CheckQName(aTarget, false);
  if (NS_FAILED(res)) {
    rv.Throw(res);
    return nullptr;
  }

  if (FindInReadable(NS_LITERAL_STRING("?>"), aData)) {
    rv.Throw(NS_ERROR_DOM_INVALID_CHARACTER_ERR);
    return nullptr;
  }

  RefPtr<ProcessingInstruction> pi =
    NS_NewXMLProcessingInstruction(mNodeInfoManager, aTarget, aData);

  return pi.forget();
}

NS_IMETHODIMP
nsDocument::CreateAttribute(const nsAString& aName,
                            nsIDOMAttr** aReturn)
{
  ErrorResult rv;
  *aReturn = nsIDocument::CreateAttribute(aName, rv).take();
  return rv.StealNSResult();
}

already_AddRefed<Attr>
nsIDocument::CreateAttribute(const nsAString& aName, ErrorResult& rv)
{
  WarnOnceAbout(eCreateAttribute);

  if (!mNodeInfoManager) {
    rv.Throw(NS_ERROR_NOT_INITIALIZED);
    return nullptr;
  }

  nsresult res = nsContentUtils::CheckQName(aName, false);
  if (NS_FAILED(res)) {
    rv.Throw(res);
    return nullptr;
  }

  nsAutoString name;
  if (IsHTMLDocument()) {
    nsContentUtils::ASCIIToLower(aName, name);
  } else {
    name = aName;
  }

  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  res = mNodeInfoManager->GetNodeInfo(name, nullptr, kNameSpaceID_None,
                                      nsIDOMNode::ATTRIBUTE_NODE,
                                      getter_AddRefs(nodeInfo));
  if (NS_FAILED(res)) {
    rv.Throw(res);
    return nullptr;
  }

  RefPtr<Attr> attribute = new Attr(nullptr, nodeInfo.forget(),
                                    EmptyString());
  return attribute.forget();
}

NS_IMETHODIMP
nsDocument::CreateAttributeNS(const nsAString & aNamespaceURI,
                              const nsAString & aQualifiedName,
                              nsIDOMAttr **aResult)
{
  ErrorResult rv;
  *aResult =
    nsIDocument::CreateAttributeNS(aNamespaceURI, aQualifiedName, rv).take();
  return rv.StealNSResult();
}

already_AddRefed<Attr>
nsIDocument::CreateAttributeNS(const nsAString& aNamespaceURI,
                               const nsAString& aQualifiedName,
                               ErrorResult& rv)
{
  WarnOnceAbout(eCreateAttributeNS);

  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  rv = nsContentUtils::GetNodeInfoFromQName(aNamespaceURI,
                                            aQualifiedName,
                                            mNodeInfoManager,
                                            nsIDOMNode::ATTRIBUTE_NODE,
                                            getter_AddRefs(nodeInfo));
  if (rv.Failed()) {
    return nullptr;
  }

  RefPtr<Attr> attribute = new Attr(nullptr, nodeInfo.forget(),
                                    EmptyString());
  return attribute.forget();
}

bool
nsDocument::CustomElementConstructor(JSContext* aCx, unsigned aArgc, JS::Value* aVp)
{
  JS::CallArgs args = JS::CallArgsFromVp(aArgc, aVp);

  JS::Rooted<JSObject*> global(aCx,
    JS_GetGlobalForObject(aCx, &args.callee()));
  nsCOMPtr<nsPIDOMWindow> window = do_QueryWrapper(aCx, global);
  MOZ_ASSERT(window, "Should have a non-null window");

  nsDocument* document = static_cast<nsDocument*>(window->GetDoc());

  // Function name is the type of the custom element.
  JSString* jsFunName =
    JS_GetFunctionId(JS_ValueToFunction(aCx, args.calleev()));
  nsAutoJSString elemName;
  if (!elemName.init(aCx, jsFunName)) {
    return true;
  }

  nsCOMPtr<nsIAtom> typeAtom(NS_Atomize(elemName));
  CustomElementHashKey key(kNameSpaceID_Unknown, typeAtom);
  CustomElementDefinition* definition;
  if (!document->mRegistry ||
      !document->mRegistry->mCustomDefinitions.Get(&key, &definition)) {
    return true;
  }

  nsDependentAtomString localName(definition->mLocalName);

  nsCOMPtr<Element> element =
    document->CreateElem(localName, nullptr, definition->mNamespaceID);
  NS_ENSURE_TRUE(element, true);

  if (definition->mLocalName != typeAtom) {
    // This element is a custom element by extension, thus we need to
    // do some special setup. For non-extended custom elements, this happens
    // when the element is created.
    document->SetupCustomElement(element, definition->mNamespaceID, &elemName);
  }

  nsresult rv = nsContentUtils::WrapNative(aCx, element, element, args.rval());
  NS_ENSURE_SUCCESS(rv, true);

  return true;
}

bool
nsDocument::IsWebComponentsEnabled(JSContext* aCx, JSObject* aObject)
{
  JS::Rooted<JSObject*> obj(aCx, aObject);

  if (Preferences::GetBool("dom.webcomponents.enabled")) {
    return true;
  }

  // Check for the webcomponents permission. See Bug 1181555.
  JSAutoCompartment ac(aCx, obj);
  JS::Rooted<JSObject*> global(aCx, JS_GetGlobalForObject(aCx, obj));
  nsCOMPtr<nsPIDOMWindow> window =
    do_QueryInterface(nsJSUtils::GetStaticScriptGlobal(global));

  if (window) {
    nsresult rv;
    nsCOMPtr<nsIPermissionManager> permMgr =
      do_GetService(NS_PERMISSIONMANAGER_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, false);

    uint32_t perm;
    rv = permMgr->TestPermissionFromWindow(
      window, "moz-extremely-unstable-and-will-change-webcomponents", &perm);
    NS_ENSURE_SUCCESS(rv, false);

    return perm == nsIPermissionManager::ALLOW_ACTION;
  }

  return false;
}

nsresult
nsDocument::RegisterUnresolvedElement(Element* aElement, nsIAtom* aTypeName)
{
  if (!mRegistry) {
    return NS_OK;
  }

  mozilla::dom::NodeInfo* info = aElement->NodeInfo();

  // Candidate may be a custom element through extension,
  // in which case the custom element type name will not
  // match the element tag name. e.g. <button is="x-button">.
  nsCOMPtr<nsIAtom> typeName = aTypeName;
  if (!typeName) {
    typeName = info->NameAtom();
  }

  CustomElementHashKey key(info->NamespaceID(), typeName);
  if (mRegistry->mCustomDefinitions.Get(&key)) {
    return NS_OK;
  }

  nsTArray<nsWeakPtr>* unresolved;
  mRegistry->mCandidatesMap.Get(&key, &unresolved);
  if (!unresolved) {
    unresolved = new nsTArray<nsWeakPtr>();
    // Ownership of unresolved is taken by mCandidatesMap.
    mRegistry->mCandidatesMap.Put(&key, unresolved);
  }

  nsWeakPtr* elem = unresolved->AppendElement();
  *elem = do_GetWeakReference(aElement);
  aElement->AddStates(NS_EVENT_STATE_UNRESOLVED);

  return NS_OK;
}

void
nsDocument::EnqueueLifecycleCallback(nsIDocument::ElementCallbackType aType,
                                     Element* aCustomElement,
                                     LifecycleCallbackArgs* aArgs,
                                     CustomElementDefinition* aDefinition)
{
  if (!mRegistry) {
    // The element might not belong to a document that
    // has a browsing context, and thus no registry.
    return;
  }

  CustomElementData* elementData = aCustomElement->GetCustomElementData();

  // Let DEFINITION be ELEMENT's definition
  CustomElementDefinition* definition = aDefinition;
  if (!definition) {
    mozilla::dom::NodeInfo* info = aCustomElement->NodeInfo();

    // Make sure we get the correct definition in case the element
    // is a extended custom element e.g. <button is="x-button">.
    nsCOMPtr<nsIAtom> typeAtom = elementData ?
      elementData->mType.get() : info->NameAtom();

    CustomElementHashKey key(info->NamespaceID(), typeAtom);
    if (!mRegistry->mCustomDefinitions.Get(&key, &definition) ||
        definition->mLocalName != info->NameAtom()) {
      // Trying to enqueue a callback for an element that is not
      // a custom element. We are done, nothing to do.
      return;
    }
  }

  if (!elementData) {
    // Create the custom element data the first time
    // that we try to enqueue a callback.
    elementData = new CustomElementData(definition->mType);
    // aCustomElement takes ownership of elementData
    aCustomElement->SetCustomElementData(elementData);
    MOZ_ASSERT(aType == nsIDocument::eCreated,
               "First callback should be the created callback");
  }

  // Let CALLBACK be the callback associated with the key NAME in CALLBACKS.
  CallbackFunction* func = nullptr;
  switch (aType) {
    case nsIDocument::eCreated:
      if (definition->mCallbacks->mCreatedCallback.WasPassed()) {
        func = definition->mCallbacks->mCreatedCallback.Value();
      }
      break;

    case nsIDocument::eAttached:
      if (definition->mCallbacks->mAttachedCallback.WasPassed()) {
        func = definition->mCallbacks->mAttachedCallback.Value();
      }
      break;

    case nsIDocument::eDetached:
      if (definition->mCallbacks->mDetachedCallback.WasPassed()) {
        func = definition->mCallbacks->mDetachedCallback.Value();
      }
      break;

    case nsIDocument::eAttributeChanged:
      if (definition->mCallbacks->mAttributeChangedCallback.WasPassed()) {
        func = definition->mCallbacks->mAttributeChangedCallback.Value();
      }
      break;
  }

  // If there is no such callback, stop.
  if (!func) {
    return;
  }

  if (aType == nsIDocument::eCreated) {
    elementData->mCreatedCallbackInvoked = false;
  } else if (!elementData->mCreatedCallbackInvoked) {
    // Callbacks other than created callback must not be enqueued
    // until after the created callback has been invoked.
    return;
  }

  // Add CALLBACK to ELEMENT's callback queue.
  CustomElementCallback* callback = new CustomElementCallback(aCustomElement,
                                                              aType,
                                                              func,
                                                              elementData);
  // Ownership of callback is taken by mCallbackQueue.
  elementData->mCallbackQueue.AppendElement(callback);
  if (aArgs) {
    callback->SetArgs(*aArgs);
  }

  if (!elementData->mElementIsBeingCreated) {
    CustomElementData* lastData =
      sProcessingStack->SafeLastElement(nullptr);

    // A new element queue needs to be pushed if the queue at the
    // top of the stack is associated with another microtask level.
    bool shouldPushElementQueue =
      (!lastData || lastData->mAssociatedMicroTask <
         static_cast<int32_t>(nsContentUtils::MicroTaskLevel()));

    // Push a new element queue onto the processing stack when appropriate
    // (when we enter a new microtask).
    if (shouldPushElementQueue) {
      // Push a sentinel value on the processing stack to mark the
      // boundary between the element queues.
      sProcessingStack->AppendElement((CustomElementData*) nullptr);
    }

    sProcessingStack->AppendElement(elementData);
    elementData->mAssociatedMicroTask =
      static_cast<int32_t>(nsContentUtils::MicroTaskLevel());

    // Add a script runner to pop and process the element queue at
    // the top of the processing stack.
    if (shouldPushElementQueue) {
      // Lifecycle callbacks enqueued by user agent implementation
      // should be invoked prior to returning control back to script.
      // Create a script runner to process the top of the processing
      // stack as soon as it is safe to run script.
      nsCOMPtr<nsIRunnable> runnable =
        NS_NewRunnableFunction(&nsDocument::ProcessTopElementQueue);
      nsContentUtils::AddScriptRunner(runnable);
    }
  }
}

// static
void
nsDocument::ProcessTopElementQueue()
{
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());

  nsTArray<RefPtr<CustomElementData>>& stack = *sProcessingStack;
  uint32_t firstQueue = stack.LastIndexOf((CustomElementData*) nullptr);

  for (uint32_t i = firstQueue + 1; i < stack.Length(); ++i) {
    // Callback queue may have already been processed in an earlier
    // element queue or in an element queue that was popped
    // off more recently.
    if (stack[i]->mAssociatedMicroTask != -1) {
      stack[i]->RunCallbackQueue();
      stack[i]->mAssociatedMicroTask = -1;
    }
  }

  // If this was actually the base element queue, don't bother trying to pop
  // the first "queue" marker (sentinel).
  if (firstQueue != 0) {
    stack.SetLength(firstQueue);
  } else {
    // Don't pop sentinel for base element queue.
    stack.SetLength(1);
  }
}

bool
nsDocument::RegisterEnabled()
{
  static bool sPrefValue =
    Preferences::GetBool("dom.webcomponents.enabled", false);
  return sPrefValue;
}

// static
Maybe<nsTArray<RefPtr<mozilla::dom::CustomElementData>>>
nsDocument::sProcessingStack;

void
nsDocument::RegisterElement(JSContext* aCx, const nsAString& aType,
                            const ElementRegistrationOptions& aOptions,
                            JS::MutableHandle<JSObject*> aRetval,
                            ErrorResult& rv)
{
  if (!mRegistry) {
    rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return;
  }

  Registry::DefinitionMap& definitions = mRegistry->mCustomDefinitions;

  // Unconditionally convert TYPE to lowercase.
  nsAutoString lcType;
  nsContentUtils::ASCIIToLower(aType, lcType);

  // Only convert NAME to lowercase in HTML documents. Note that NAME is
  // options.extends.
  nsAutoString lcName;
  if (IsHTMLDocument()) {
    nsContentUtils::ASCIIToLower(aOptions.mExtends, lcName);
  } else {
    lcName.Assign(aOptions.mExtends);
  }

  nsCOMPtr<nsIAtom> typeAtom(NS_Atomize(lcType));
  if (!nsContentUtils::IsCustomElementName(typeAtom)) {
    rv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    return;
  }

  // If there already exists a definition with the same TYPE, set ERROR to
  // DuplicateDefinition and stop.
  // Note that we need to find existing custom elements from either namespace.
  CustomElementHashKey duplicateFinder(kNameSpaceID_Unknown, typeAtom);
  if (definitions.Get(&duplicateFinder)) {
    rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return;
  }

  nsIGlobalObject* sgo = GetScopeObject();
  if (!sgo) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  JS::Rooted<JSObject*> global(aCx, sgo->GetGlobalJSObject());
  nsCOMPtr<nsIAtom> nameAtom;
  int32_t namespaceID = kNameSpaceID_XHTML;
  JS::Rooted<JSObject*> protoObject(aCx);
  {
    JS::Rooted<JSObject*> htmlProto(aCx);
    JS::Rooted<JSObject*> svgProto(aCx);
    {
      JSAutoCompartment ac(aCx, global);

      htmlProto = HTMLElementBinding::GetProtoObjectHandle(aCx, global);
      if (!htmlProto) {
        rv.Throw(NS_ERROR_OUT_OF_MEMORY);
        return;
      }

      svgProto = SVGElementBinding::GetProtoObjectHandle(aCx, global);
      if (!svgProto) {
        rv.Throw(NS_ERROR_OUT_OF_MEMORY);
        return;
      }
    }

    if (!aOptions.mPrototype) {
      if (!JS_WrapObject(aCx, &htmlProto)) {
        rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
        return;
      }

      protoObject = JS_NewObjectWithGivenProto(aCx, nullptr, htmlProto);
      if (!protoObject) {
        rv.Throw(NS_ERROR_UNEXPECTED);
        return;
      }
    } else {
      protoObject = aOptions.mPrototype;

      // Get the unwrapped prototype to do some checks.
      JS::Rooted<JSObject*> protoObjectUnwrapped(aCx, js::CheckedUnwrap(protoObject));
      if (!protoObjectUnwrapped) {
        // If the caller's compartment does not have permission to access the
        // unwrapped prototype then throw.
        rv.Throw(NS_ERROR_DOM_SECURITY_ERR);
        return;
      }

      // If PROTOTYPE is already an interface prototype object for any interface
      // object or PROTOTYPE has a non-configurable property named constructor,
      // throw a NotSupportedError and stop.
      const js::Class* clasp = js::GetObjectClass(protoObjectUnwrapped);
      if (IsDOMIfaceAndProtoClass(clasp)) {
        rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
        return;
      }

      JS::Rooted<JS::PropertyDescriptor> descRoot(aCx);
      JS::MutableHandle<JS::PropertyDescriptor> desc(&descRoot);
      // This check may go through a wrapper, but as we checked above
      // it should be transparent or an xray. This should be fine for now,
      // until the spec is sorted out.
      if (!JS_GetPropertyDescriptor(aCx, protoObject, "constructor", desc)) {
        rv.Throw(NS_ERROR_UNEXPECTED);
        return;
      }

      if (!desc.configurable()) {
        rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
        return;
      }

      JS::Rooted<JSObject*> protoProto(aCx, protoObject);

      if (!JS_WrapObject(aCx, &htmlProto) || !JS_WrapObject(aCx, &svgProto)) {
        rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
        return;
      }

      // If PROTOTYPE's interface inherits from SVGElement, set NAMESPACE to SVG
      // Namespace.
      while (protoProto) {
        if (protoProto == htmlProto) {
          break;
        }

        if (protoProto == svgProto) {
          namespaceID = kNameSpaceID_SVG;
          break;
        }

        if (!JS_GetPrototype(aCx, protoProto, &protoProto)) {
          rv.Throw(NS_ERROR_UNEXPECTED);
          return;
        }
      }
    } // Done with the checks, leave prototype's compartment.

    // If name was provided and not null...
    if (!lcName.IsEmpty()) {
      // Let BASE be the element interface for NAME and NAMESPACE.
      bool known = false;
      nameAtom = NS_Atomize(lcName);
      if (namespaceID == kNameSpaceID_XHTML) {
        nsIParserService* ps = nsContentUtils::GetParserService();
        if (!ps) {
          rv.Throw(NS_ERROR_UNEXPECTED);
          return;
        }

        known =
          ps->HTMLCaseSensitiveAtomTagToId(nameAtom) != eHTMLTag_userdefined;
      } else {
        known = SVGElementFactory::Exists(nameAtom);
      }

      // If BASE does not exist or is an interface for a custom element, set ERROR
      // to InvalidName and stop.
      // If BASE exists, then it cannot be an interface for a custom element.
      if (!known) {
        rv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
        return;
      }
    } else {
      // If NAMESPACE is SVG Namespace, set ERROR to InvalidName and stop.
      if (namespaceID == kNameSpaceID_SVG) {
        rv.Throw(NS_ERROR_UNEXPECTED);
        return;
      }

      nameAtom = typeAtom;
    }
  } // Leaving the document's compartment for the LifecycleCallbacks init

  JS::Rooted<JSObject*> wrappedProto(aCx, protoObject);
  if (!JS_WrapObject(aCx, &wrappedProto)) {
    rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return;
  }

  // Note: We call the init from the caller compartment here
  nsAutoPtr<LifecycleCallbacks> callbacksHolder(new LifecycleCallbacks());
  JS::RootedValue rootedv(aCx, JS::ObjectValue(*wrappedProto));
  if (!JS_WrapValue(aCx, &rootedv) || !callbacksHolder->Init(aCx, rootedv)) {
    rv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // Associate the definition with the custom element.
  CustomElementHashKey key(namespaceID, typeAtom);
  LifecycleCallbacks* callbacks = callbacksHolder.forget();
  CustomElementDefinition* definition =
    new CustomElementDefinition(wrappedProto,
                                typeAtom,
                                nameAtom,
                                callbacks,
                                namespaceID,
                                0 /* TODO dependent on HTML imports. Bug 877072 */);
  definitions.Put(&key, definition);

  // Do element upgrade.
  nsAutoPtr<nsTArray<nsWeakPtr>> candidates;
  mRegistry->mCandidatesMap.RemoveAndForget(&key, candidates);
  if (candidates) {
    for (size_t i = 0; i < candidates->Length(); ++i) {
      nsCOMPtr<Element> elem = do_QueryReferent(candidates->ElementAt(i));
      if (!elem) {
        continue;
      }

      elem->RemoveStates(NS_EVENT_STATE_UNRESOLVED);

      // Make sure that the element name matches the name in the definition.
      // (e.g. a definition for x-button extending button should match
      // <button is="x-button"> but not <x-button>.
      if (elem->NodeInfo()->NameAtom() != nameAtom) {
        //Skip over this element because definition does not apply.
        continue;
      }

      MOZ_ASSERT(elem->IsHTMLElement(nameAtom));
      nsWrapperCache* cache;
      CallQueryInterface(elem, &cache);
      MOZ_ASSERT(cache, "Element doesn't support wrapper cache?");

      // We want to set the custom prototype in the caller's comparment.
      // In the case that element is in a different compartment,
      // this will set the prototype on the element's wrapper and
      // thus only visible in the wrapper's compartment.
      JS::RootedObject wrapper(aCx);
      if ((wrapper = cache->GetWrapper()) && JS_WrapObject(aCx, &wrapper)) {
        if (!JS_SetPrototype(aCx, wrapper, wrappedProto)) {
          continue;
        }
      }

      EnqueueLifecycleCallback(nsIDocument::eCreated, elem, nullptr, definition);
    }
  }

  JS::Rooted<JSFunction*> constructor(aCx);

  {
    // Go into the document's global compartment when creating the constructor
    // function because we want to get the correct document (where the
    // definition is registered) when it is called.
    JSAutoCompartment ac(aCx, global);

    // Create constructor to return. Store the name of the custom element as the
    // name of the function.
    constructor = JS_NewFunction(aCx, nsDocument::CustomElementConstructor, 0,
                                 JSFUN_CONSTRUCTOR,
                                 NS_ConvertUTF16toUTF8(lcType).get());
    if (!constructor) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
  }

  JS::Rooted<JSObject*> wrappedConstructor(aCx);
  wrappedConstructor = JS_GetFunctionObject(constructor);
  if (!JS_WrapObject(aCx, &wrappedConstructor)) {
    rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return;
  }

  if (!JS_LinkConstructorAndPrototype(aCx, wrappedConstructor, protoObject)) {
    rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return;
  }

  aRetval.set(wrappedConstructor);
}

void
nsDocument::UseRegistryFromDocument(nsIDocument* aDocument)
{
  nsDocument* doc = static_cast<nsDocument*>(aDocument);
  MOZ_ASSERT(!mRegistry, "There should be no existing registry.");
  mRegistry = doc->mRegistry;
}

NS_IMETHODIMP
nsDocument::GetElementsByTagName(const nsAString& aTagname,
                                 nsIDOMNodeList** aReturn)
{
  RefPtr<nsContentList> list = GetElementsByTagName(aTagname);
  NS_ENSURE_TRUE(list, NS_ERROR_OUT_OF_MEMORY);

  // transfer ref to aReturn
  list.forget(aReturn);
  return NS_OK;
}

long
nsDocument::BlockedTrackingNodeCount() const
{
  return mBlockedTrackingNodes.Length();
}

already_AddRefed<nsSimpleContentList>
nsDocument::BlockedTrackingNodes() const
{
  RefPtr<nsSimpleContentList> list = new nsSimpleContentList(nullptr);

  nsTArray<nsWeakPtr> blockedTrackingNodes;
  blockedTrackingNodes = mBlockedTrackingNodes;

  for (unsigned long i = 0; i < blockedTrackingNodes.Length(); i++) {
    nsWeakPtr weakNode = blockedTrackingNodes[i];
    nsCOMPtr<nsIContent> node = do_QueryReferent(weakNode);
    // Consider only nodes to which we have managed to get strong references.
    // Coping with nullptrs since it's expected for nodes to disappear when
    // nobody else is referring to them.
    if (node) {
      list->AppendElement(node);
    }
  }

  return list.forget();
}

already_AddRefed<nsContentList>
nsIDocument::GetElementsByTagNameNS(const nsAString& aNamespaceURI,
                                    const nsAString& aLocalName,
                                    ErrorResult& aResult)
{
  int32_t nameSpaceId = kNameSpaceID_Wildcard;

  if (!aNamespaceURI.EqualsLiteral("*")) {
    aResult =
      nsContentUtils::NameSpaceManager()->RegisterNameSpace(aNamespaceURI,
                                                            nameSpaceId);
    if (aResult.Failed()) {
      return nullptr;
    }
  }

  NS_ASSERTION(nameSpaceId != kNameSpaceID_Unknown, "Unexpected namespace ID!");

  return NS_GetContentList(this, nameSpaceId, aLocalName);
}

NS_IMETHODIMP
nsDocument::GetElementsByTagNameNS(const nsAString& aNamespaceURI,
                                   const nsAString& aLocalName,
                                   nsIDOMNodeList** aReturn)
{
  ErrorResult rv;
  RefPtr<nsContentList> list =
    nsIDocument::GetElementsByTagNameNS(aNamespaceURI, aLocalName, rv);
  if (rv.Failed()) {
    return rv.StealNSResult();
  }

  // transfer ref to aReturn
  list.forget(aReturn);
  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetAsync(bool *aAsync)
{
  NS_ERROR("nsDocument::GetAsync() should be overriden by subclass!");

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDocument::SetAsync(bool aAsync)
{
  NS_ERROR("nsDocument::SetAsync() should be overriden by subclass!");

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDocument::Load(const nsAString& aUrl, bool *aReturn)
{
  NS_ERROR("nsDocument::Load() should be overriden by subclass!");

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDocument::GetStyleSheets(nsIDOMStyleSheetList** aStyleSheets)
{
  NS_ADDREF(*aStyleSheets = StyleSheets());
  return NS_OK;
}

StyleSheetList*
nsDocument::StyleSheets()
{
  if (!mDOMStyleSheets) {
    mDOMStyleSheets = new nsDOMStyleSheetList(this);
  }
  return mDOMStyleSheets;
}

NS_IMETHODIMP
nsDocument::GetMozSelectedStyleSheetSet(nsAString& aSheetSet)
{
  nsIDocument::GetSelectedStyleSheetSet(aSheetSet);
  return NS_OK;
}

void
nsIDocument::GetSelectedStyleSheetSet(nsAString& aSheetSet)
{
  aSheetSet.Truncate();

  // Look through our sheets, find the selected set title
  int32_t count = GetNumberOfStyleSheets();
  nsAutoString title;
  for (int32_t index = 0; index < count; index++) {
    StyleSheetHandle sheet = GetStyleSheetAt(index);
    NS_ASSERTION(sheet, "Null sheet in sheet list!");

    // XXXheycam Make this work with ServoStyleSheets.
    if (sheet->IsServo()) {
      NS_ERROR("stylo: can't handle alternate ServoStyleSheets yet");
      continue;
    }

    bool disabled;
    sheet->AsGecko()->GetDisabled(&disabled);
    if (disabled) {
      // Disabled sheets don't affect the currently selected set
      continue;
    }

    sheet->AsGecko()->GetTitle(title);

    if (aSheetSet.IsEmpty()) {
      aSheetSet = title;
    } else if (!title.IsEmpty() && !aSheetSet.Equals(title)) {
      // Sheets from multiple sets enabled; return null string, per spec.
      SetDOMStringToNull(aSheetSet);
      return;
    }
  }
}

NS_IMETHODIMP
nsDocument::SetMozSelectedStyleSheetSet(const nsAString& aSheetSet)
{
  SetSelectedStyleSheetSet(aSheetSet);
  return NS_OK;
}

void
nsDocument::SetSelectedStyleSheetSet(const nsAString& aSheetSet)
{
  if (DOMStringIsNull(aSheetSet)) {
    return;
  }

  // Must update mLastStyleSheetSet before doing anything else with stylesheets
  // or CSSLoaders.
  mLastStyleSheetSet = aSheetSet;
  EnableStyleSheetsForSetInternal(aSheetSet, true);
}

NS_IMETHODIMP
nsDocument::GetLastStyleSheetSet(nsAString& aSheetSet)
{
  nsString sheetSet;
  GetLastStyleSheetSet(sheetSet);
  aSheetSet = sheetSet;
  return NS_OK;
}

void
nsDocument::GetLastStyleSheetSet(nsString& aSheetSet)
{
  aSheetSet = mLastStyleSheetSet;
}

NS_IMETHODIMP
nsDocument::GetPreferredStyleSheetSet(nsAString& aSheetSet)
{
  nsIDocument::GetPreferredStyleSheetSet(aSheetSet);
  return NS_OK;
}

void
nsIDocument::GetPreferredStyleSheetSet(nsAString& aSheetSet)
{
  GetHeaderData(nsGkAtoms::headerDefaultStyle, aSheetSet);
}

NS_IMETHODIMP
nsDocument::GetStyleSheetSets(nsISupports** aList)
{
  NS_ADDREF(*aList = StyleSheetSets());
  return NS_OK;
}

DOMStringList*
nsDocument::StyleSheetSets()
{
  if (!mStyleSheetSetList) {
    mStyleSheetSetList = new nsDOMStyleSheetSetList(this);
  }
  return mStyleSheetSetList;
}

NS_IMETHODIMP
nsDocument::MozEnableStyleSheetsForSet(const nsAString& aSheetSet)
{
  EnableStyleSheetsForSet(aSheetSet);
  return NS_OK;
}

void
nsDocument::EnableStyleSheetsForSet(const nsAString& aSheetSet)
{
  // Per spec, passing in null is a no-op.
  if (!DOMStringIsNull(aSheetSet)) {
    // Note: must make sure to not change the CSSLoader's preferred sheet --
    // that value should be equal to either our lastStyleSheetSet (if that's
    // non-null) or to our preferredStyleSheetSet.  And this method doesn't
    // change either of those.
    EnableStyleSheetsForSetInternal(aSheetSet, false);
  }
}

void
nsDocument::EnableStyleSheetsForSetInternal(const nsAString& aSheetSet,
                                            bool aUpdateCSSLoader)
{
  BeginUpdate(UPDATE_STYLE);
  int32_t count = GetNumberOfStyleSheets();
  nsAutoString title;
  for (int32_t index = 0; index < count; index++) {
    StyleSheetHandle sheet = GetStyleSheetAt(index);
    NS_ASSERTION(sheet, "Null sheet in sheet list!");

    // XXXheycam Make this work with ServoStyleSheets.
    if (sheet->IsServo()) {
      NS_ERROR("stylo: can't handle alternate ServoStyleSheets yet");
      continue;
    }

    sheet->AsGecko()->GetTitle(title);
    if (!title.IsEmpty()) {
      sheet->AsGecko()->SetEnabled(title.Equals(aSheetSet));
    }
  }
  if (aUpdateCSSLoader) {
    CSSLoader()->SetPreferredSheet(aSheetSet);
  }
  EndUpdate(UPDATE_STYLE);
}

NS_IMETHODIMP
nsDocument::GetCharacterSet(nsAString& aCharacterSet)
{
  nsIDocument::GetCharacterSet(aCharacterSet);
  return NS_OK;
}

void
nsIDocument::GetCharacterSet(nsAString& aCharacterSet) const
{
  CopyASCIItoUTF16(GetDocumentCharacterSet(), aCharacterSet);
}

NS_IMETHODIMP
nsDocument::ImportNode(nsIDOMNode* aImportedNode,
                       bool aDeep,
                       uint8_t aArgc,
                       nsIDOMNode** aResult)
{
  if (aArgc == 0) {
    aDeep = true;
  }

  *aResult = nullptr;

  nsCOMPtr<nsINode> imported = do_QueryInterface(aImportedNode);
  NS_ENSURE_TRUE(imported, NS_ERROR_UNEXPECTED);

  ErrorResult rv;
  nsCOMPtr<nsINode> result = nsIDocument::ImportNode(*imported, aDeep, rv);
  if (rv.Failed()) {
    return rv.StealNSResult();
  }

  NS_ADDREF(*aResult = result->AsDOMNode());
  return NS_OK;
}

already_AddRefed<nsINode>
nsIDocument::ImportNode(nsINode& aNode, bool aDeep, ErrorResult& rv) const
{
  nsINode* imported = &aNode;

  switch (imported->NodeType()) {
    case nsIDOMNode::DOCUMENT_NODE:
    {
      break;
    }
    case nsIDOMNode::DOCUMENT_FRAGMENT_NODE:
    {
      if (ShadowRoot::FromNode(imported)) {
        break;
      }
      MOZ_FALLTHROUGH;
    }
    case nsIDOMNode::ATTRIBUTE_NODE:
    case nsIDOMNode::ELEMENT_NODE:
    case nsIDOMNode::PROCESSING_INSTRUCTION_NODE:
    case nsIDOMNode::TEXT_NODE:
    case nsIDOMNode::CDATA_SECTION_NODE:
    case nsIDOMNode::COMMENT_NODE:
    case nsIDOMNode::DOCUMENT_TYPE_NODE:
    {
      nsCOMPtr<nsINode> newNode;
      nsCOMArray<nsINode> nodesWithProperties;
      rv = nsNodeUtils::Clone(imported, aDeep, mNodeInfoManager,
                              nodesWithProperties, getter_AddRefs(newNode));
      if (rv.Failed()) {
        return nullptr;
      }
      return newNode.forget();
    }
    default:
    {
      NS_WARNING("Don't know how to clone this nodetype for importNode.");
    }
  }

  rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  return nullptr;
}

NS_IMETHODIMP
nsDocument::LoadBindingDocument(const nsAString& aURI)
{
  ErrorResult rv;
  nsIDocument::LoadBindingDocument(aURI, rv);
  return rv.StealNSResult();
}

void
nsIDocument::LoadBindingDocument(const nsAString& aURI, ErrorResult& rv)
{
  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), aURI,
                 mCharacterSet.get(),
                 GetDocBaseURI());
  if (rv.Failed()) {
    return;
  }

  // Note - This computation of subjectPrincipal isn't necessarily sensical.
  // It's just designed to preserve the old semantics during a mass-conversion
  // patch.
  nsCOMPtr<nsIPrincipal> subjectPrincipal =
    nsContentUtils::GetCurrentJSContext() ? nsContentUtils::SubjectPrincipal()
                                          : NodePrincipal();
  BindingManager()->LoadBindingDocument(this, uri, subjectPrincipal);
}

NS_IMETHODIMP
nsDocument::GetBindingParent(nsIDOMNode* aNode, nsIDOMElement** aResult)
{
  nsCOMPtr<nsINode> node = do_QueryInterface(aNode);
  NS_ENSURE_ARG_POINTER(node);

  Element* bindingParent = nsIDocument::GetBindingParent(*node);
  nsCOMPtr<nsIDOMElement> retval = do_QueryInterface(bindingParent);
  retval.forget(aResult);
  return NS_OK;
}

Element*
nsIDocument::GetBindingParent(nsINode& aNode)
{
  nsCOMPtr<nsIContent> content(do_QueryInterface(&aNode));
  if (!content)
    return nullptr;

  nsIContent* bindingParent = content->GetBindingParent();
  return bindingParent ? bindingParent->AsElement() : nullptr;
}

static Element*
GetElementByAttribute(nsIContent* aContent, nsIAtom* aAttrName,
                      const nsAString& aAttrValue, bool aUniversalMatch)
{
  if (aUniversalMatch ? aContent->HasAttr(kNameSpaceID_None, aAttrName) :
                        aContent->AttrValueIs(kNameSpaceID_None, aAttrName,
                                              aAttrValue, eCaseMatters)) {
    return aContent->AsElement();
  }

  for (nsIContent* child = aContent->GetFirstChild();
       child;
       child = child->GetNextSibling()) {

    Element* matchedElement =
      GetElementByAttribute(child, aAttrName, aAttrValue, aUniversalMatch);
    if (matchedElement)
      return matchedElement;
  }

  return nullptr;
}

Element*
nsDocument::GetAnonymousElementByAttribute(nsIContent* aElement,
                                           nsIAtom* aAttrName,
                                           const nsAString& aAttrValue) const
{
  nsINodeList* nodeList = BindingManager()->GetAnonymousNodesFor(aElement);
  if (!nodeList)
    return nullptr;

  uint32_t length = 0;
  nodeList->GetLength(&length);

  bool universalMatch = aAttrValue.EqualsLiteral("*");

  for (uint32_t i = 0; i < length; ++i) {
    nsIContent* current = nodeList->Item(i);
    Element* matchedElm =
      GetElementByAttribute(current, aAttrName, aAttrValue, universalMatch);
    if (matchedElm)
      return matchedElm;
  }

  return nullptr;
}

NS_IMETHODIMP
nsDocument::GetAnonymousElementByAttribute(nsIDOMElement* aElement,
                                           const nsAString& aAttrName,
                                           const nsAString& aAttrValue,
                                           nsIDOMElement** aResult)
{
  nsCOMPtr<Element> element = do_QueryInterface(aElement);
  NS_ENSURE_ARG_POINTER(element);

  Element* anonEl =
    nsIDocument::GetAnonymousElementByAttribute(*element, aAttrName,
                                                aAttrValue);
  nsCOMPtr<nsIDOMElement> retval = do_QueryInterface(anonEl);
  retval.forget(aResult);
  return NS_OK;
}

Element*
nsIDocument::GetAnonymousElementByAttribute(Element& aElement,
                                            const nsAString& aAttrName,
                                            const nsAString& aAttrValue)
{
  nsCOMPtr<nsIAtom> attribute = NS_Atomize(aAttrName);

  return GetAnonymousElementByAttribute(&aElement, attribute, aAttrValue);
}


NS_IMETHODIMP
nsDocument::GetAnonymousNodes(nsIDOMElement* aElement,
                              nsIDOMNodeList** aResult)
{
  *aResult = nullptr;

  nsCOMPtr<nsIContent> content(do_QueryInterface(aElement));
  return BindingManager()->GetAnonymousNodesFor(content, aResult);
}

nsINodeList*
nsIDocument::GetAnonymousNodes(Element& aElement)
{
  return BindingManager()->GetAnonymousNodesFor(&aElement);
}

NS_IMETHODIMP
nsDocument::CreateRange(nsIDOMRange** aReturn)
{
  ErrorResult rv;
  *aReturn = nsIDocument::CreateRange(rv).take();
  return rv.StealNSResult();
}

already_AddRefed<nsRange>
nsIDocument::CreateRange(ErrorResult& rv)
{
  RefPtr<nsRange> range = new nsRange(this);
  nsresult res = range->Set(this, 0, this, 0);
  if (NS_FAILED(res)) {
    rv.Throw(res);
    return nullptr;
  }

  return range.forget();
}

NS_IMETHODIMP
nsDocument::CreateNodeIterator(nsIDOMNode *aRoot,
                               uint32_t aWhatToShow,
                               nsIDOMNodeFilter *aFilter,
                               uint8_t aOptionalArgc,
                               nsIDOMNodeIterator **_retval)
{
  *_retval = nullptr;

  if (!aOptionalArgc) {
    aWhatToShow = nsIDOMNodeFilter::SHOW_ALL;
  }

  if (!aRoot) {
    return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }

  nsCOMPtr<nsINode> root = do_QueryInterface(aRoot);
  NS_ENSURE_TRUE(root, NS_ERROR_UNEXPECTED);

  ErrorResult rv;
  NodeFilterHolder holder(aFilter);
  *_retval = nsIDocument::CreateNodeIterator(*root, aWhatToShow, holder,
                                             rv).take();
  return rv.StealNSResult();
}

already_AddRefed<NodeIterator>
nsIDocument::CreateNodeIterator(nsINode& aRoot, uint32_t aWhatToShow,
                                NodeFilter* aFilter,
                                ErrorResult& rv) const
{
  NodeFilterHolder holder(aFilter);
  return CreateNodeIterator(aRoot, aWhatToShow, holder, rv);
}

already_AddRefed<NodeIterator>
nsIDocument::CreateNodeIterator(nsINode& aRoot, uint32_t aWhatToShow,
                                const NodeFilterHolder& aFilter,
                                ErrorResult& rv) const
{
  nsINode* root = &aRoot;
  RefPtr<NodeIterator> iterator = new NodeIterator(root, aWhatToShow,
                                                     aFilter);
  return iterator.forget();
}

NS_IMETHODIMP
nsDocument::CreateTreeWalker(nsIDOMNode *aRoot,
                             uint32_t aWhatToShow,
                             nsIDOMNodeFilter *aFilter,
                             uint8_t aOptionalArgc,
                             nsIDOMTreeWalker **_retval)
{
  *_retval = nullptr;

  if (!aOptionalArgc) {
    aWhatToShow = nsIDOMNodeFilter::SHOW_ALL;
  }

  nsCOMPtr<nsINode> root = do_QueryInterface(aRoot);
  NS_ENSURE_TRUE(root, NS_ERROR_DOM_NOT_SUPPORTED_ERR);

  ErrorResult rv;
  NodeFilterHolder holder(aFilter);
  *_retval = nsIDocument::CreateTreeWalker(*root, aWhatToShow, holder,
                                           rv).take();
  return rv.StealNSResult();
}

already_AddRefed<TreeWalker>
nsIDocument::CreateTreeWalker(nsINode& aRoot, uint32_t aWhatToShow,
                              NodeFilter* aFilter,
                              ErrorResult& rv) const
{
  NodeFilterHolder holder(aFilter);
  return CreateTreeWalker(aRoot, aWhatToShow, holder, rv);
}

already_AddRefed<TreeWalker>
nsIDocument::CreateTreeWalker(nsINode& aRoot, uint32_t aWhatToShow,
                              const NodeFilterHolder& aFilter,
                              ErrorResult& rv) const
{
  nsINode* root = &aRoot;
  RefPtr<TreeWalker> walker = new TreeWalker(root, aWhatToShow, aFilter);
  return walker.forget();
}


NS_IMETHODIMP
nsDocument::GetDefaultView(nsIDOMWindow** aDefaultView)
{
  *aDefaultView = nullptr;
  nsCOMPtr<nsPIDOMWindow> win = GetWindow();
  win.forget(aDefaultView);
  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetLocation(nsIDOMLocation **_retval)
{
  *_retval = nsIDocument::GetLocation().take();
  return NS_OK;
}

already_AddRefed<nsLocation>
nsIDocument::GetLocation() const
{
  nsCOMPtr<nsIDOMWindow> w = do_QueryInterface(mScriptGlobalObject);

  if (!w) {
    return nullptr;
  }

  nsCOMPtr<nsIDOMLocation> loc;
  w->GetLocation(getter_AddRefs(loc));
  return loc.forget().downcast<nsLocation>();
}

Element*
nsIDocument::GetHtmlElement() const
{
  Element* rootElement = GetRootElement();
  if (rootElement && rootElement->IsHTMLElement(nsGkAtoms::html))
    return rootElement;
  return nullptr;
}

Element*
nsIDocument::GetHtmlChildElement(nsIAtom* aTag)
{
  Element* html = GetHtmlElement();
  if (!html)
    return nullptr;

  // Look for the element with aTag inside html. This needs to run
  // forwards to find the first such element.
  for (nsIContent* child = html->GetFirstChild();
       child;
       child = child->GetNextSibling()) {
    if (child->IsHTMLElement(aTag))
      return child->AsElement();
  }
  return nullptr;
}

Element*
nsDocument::GetTitleElement()
{
  // mMayHaveTitleElement will have been set to true if any HTML or SVG
  // <title> element has been bound to this document. So if it's false,
  // we know there is nothing to do here. This avoids us having to search
  // the whole DOM if someone calls document.title on a large document
  // without a title.
  if (!mMayHaveTitleElement)
    return nullptr;

  Element* root = GetRootElement();
  if (root && root->IsSVGElement(nsGkAtoms::svg)) {
    // In SVG, the document's title must be a child
    for (nsIContent* child = root->GetFirstChild();
         child; child = child->GetNextSibling()) {
      if (child->IsSVGElement(nsGkAtoms::title)) {
        return child->AsElement();
      }
    }
    return nullptr;
  }

  // We check the HTML namespace even for non-HTML documents, except SVG.  This
  // matches the spec and the behavior of all tested browsers.
  RefPtr<nsContentList> list =
    NS_GetContentList(this, kNameSpaceID_XHTML, NS_LITERAL_STRING("title"));

  nsIContent* first = list->Item(0, false);

  return first ? first->AsElement() : nullptr;
}

NS_IMETHODIMP
nsDocument::GetTitle(nsAString& aTitle)
{
  nsString title;
  GetTitle(title);
  aTitle = title;
  return NS_OK;
}

void
nsDocument::GetTitle(nsString& aTitle)
{
  aTitle.Truncate();

  Element* rootElement = GetRootElement();
  if (!rootElement) {
    return;
  }

  nsAutoString tmp;

#ifdef MOZ_XUL
  if (rootElement->IsXULElement()) {
    rootElement->GetAttr(kNameSpaceID_None, nsGkAtoms::title, tmp);
  } else
#endif
  {
    Element* title = GetTitleElement();
    if (!title) {
      return;
    }
    nsContentUtils::GetNodeTextContent(title, false, tmp);
  }

  tmp.CompressWhitespace();
  aTitle = tmp;
}

NS_IMETHODIMP
nsDocument::SetTitle(const nsAString& aTitle)
{
  Element* rootElement = GetRootElement();
  if (!rootElement) {
    return NS_OK;
  }

#ifdef MOZ_XUL
  if (rootElement->IsXULElement()) {
    return rootElement->SetAttr(kNameSpaceID_None, nsGkAtoms::title,
                                aTitle, true);
  }
#endif

  // Batch updates so that mutation events don't change "the title
  // element" under us
  mozAutoDocUpdate updateBatch(this, UPDATE_CONTENT_MODEL, true);

  nsCOMPtr<Element> title = GetTitleElement();
  if (rootElement->IsSVGElement(nsGkAtoms::svg)) {
    if (!title) {
      RefPtr<mozilla::dom::NodeInfo> titleInfo =
        mNodeInfoManager->GetNodeInfo(nsGkAtoms::title, nullptr,
                                      kNameSpaceID_SVG,
                                      nsIDOMNode::ELEMENT_NODE);
      NS_NewSVGElement(getter_AddRefs(title), titleInfo.forget(),
                       NOT_FROM_PARSER);
      if (!title) {
        return NS_OK;
      }
      rootElement->InsertChildAt(title, 0, true);
    }
  } else if (rootElement->IsHTMLElement()) {
    if (!title) {
      Element* head = GetHeadElement();
      if (!head) {
        return NS_OK;
      }

      RefPtr<mozilla::dom::NodeInfo> titleInfo;
      titleInfo = mNodeInfoManager->GetNodeInfo(nsGkAtoms::title, nullptr,
          kNameSpaceID_XHTML,
          nsIDOMNode::ELEMENT_NODE);
      title = NS_NewHTMLTitleElement(titleInfo.forget());
      if (!title) {
        return NS_OK;
      }

      head->AppendChildTo(title, true);
    }
  } else {
    return NS_OK;
  }

  return nsContentUtils::SetNodeTextContent(title, aTitle, false);
}

void
nsDocument::SetTitle(const nsAString& aTitle, ErrorResult& rv)
{
  rv = SetTitle(aTitle);
}

void
nsDocument::NotifyPossibleTitleChange(bool aBoundTitleElement)
{
  NS_ASSERTION(!mInUnlinkOrDeletion || !aBoundTitleElement,
               "Setting a title while unlinking or destroying the element?");
  if (mInUnlinkOrDeletion) {
    return;
  }

  if (aBoundTitleElement) {
    mMayHaveTitleElement = true;
  }
  if (mPendingTitleChangeEvent.IsPending())
    return;

  RefPtr<nsRunnableMethod<nsDocument, void, false> > event =
    NewNonOwningRunnableMethod(this,
      &nsDocument::DoNotifyPossibleTitleChange);
  nsresult rv = NS_DispatchToCurrentThread(event);
  if (NS_SUCCEEDED(rv)) {
    mPendingTitleChangeEvent = event;
  }
}

void
nsDocument::DoNotifyPossibleTitleChange()
{
  mPendingTitleChangeEvent.Forget();
  mHaveFiredTitleChange = true;

  nsAutoString title;
  GetTitle(title);

  nsCOMPtr<nsIPresShell> shell = GetShell();
  if (shell) {
    nsCOMPtr<nsISupports> container =
      shell->GetPresContext()->GetContainerWeak();
    if (container) {
      nsCOMPtr<nsIBaseWindow> docShellWin = do_QueryInterface(container);
      if (docShellWin) {
        docShellWin->SetTitle(title.get());
      }
    }
  }

  // Fire a DOM event for the title change.
  nsContentUtils::DispatchChromeEvent(this, static_cast<nsIDocument*>(this),
                                      NS_LITERAL_STRING("DOMTitleChanged"),
                                      true, true);
}

already_AddRefed<BoxObject>
nsDocument::GetBoxObjectFor(Element* aElement, ErrorResult& aRv)
{
  if (!aElement) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  nsIDocument* doc = aElement->OwnerDoc();
  if (doc != this) {
    aRv.Throw(NS_ERROR_DOM_WRONG_DOCUMENT_ERR);
    return nullptr;
  }

  if (!mHasWarnedAboutBoxObjects && !aElement->IsXULElement()) {
    mHasWarnedAboutBoxObjects = true;
    nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                    NS_LITERAL_CSTRING("BoxObjects"), this,
                                    nsContentUtils::eDOM_PROPERTIES,
                                    "UseOfGetBoxObjectForWarning");
  }

  if (!mBoxObjectTable) {
    mBoxObjectTable = new nsInterfaceHashtable<nsPtrHashKey<nsIContent>, nsPIBoxObject>(6);
  } else {
    nsCOMPtr<nsPIBoxObject> boxObject = mBoxObjectTable->Get(aElement);
    if (boxObject) {
      return boxObject.forget().downcast<BoxObject>();
    }
  }

  int32_t namespaceID;
  nsCOMPtr<nsIAtom> tag = BindingManager()->ResolveTag(aElement, &namespaceID);

  nsAutoCString contractID("@mozilla.org/layout/xul-boxobject");
  if (namespaceID == kNameSpaceID_XUL) {
    if (tag == nsGkAtoms::browser ||
        tag == nsGkAtoms::editor ||
        tag == nsGkAtoms::iframe)
      contractID += "-container";
    else if (tag == nsGkAtoms::menu)
      contractID += "-menu";
    else if (tag == nsGkAtoms::popup ||
             tag == nsGkAtoms::menupopup ||
             tag == nsGkAtoms::panel ||
             tag == nsGkAtoms::tooltip)
      contractID += "-popup";
    else if (tag == nsGkAtoms::tree)
      contractID += "-tree";
    else if (tag == nsGkAtoms::listbox)
      contractID += "-listbox";
    else if (tag == nsGkAtoms::scrollbox)
      contractID += "-scrollbox";
  }
  contractID += ";1";

  nsCOMPtr<nsPIBoxObject> boxObject(do_CreateInstance(contractID.get()));
  if (!boxObject) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  boxObject->Init(aElement);

  if (mBoxObjectTable) {
    mBoxObjectTable->Put(aElement, boxObject.get());
  }

  return boxObject.forget().downcast<BoxObject>();
}

void
nsDocument::ClearBoxObjectFor(nsIContent* aContent)
{
  if (mBoxObjectTable) {
    nsPIBoxObject *boxObject = mBoxObjectTable->GetWeak(aContent);
    if (boxObject) {
      boxObject->Clear();
      mBoxObjectTable->Remove(aContent);
    }
  }
}

already_AddRefed<MediaQueryList>
nsIDocument::MatchMedia(const nsAString& aMediaQueryList)
{
  RefPtr<MediaQueryList> result = new MediaQueryList(this, aMediaQueryList);

  // Insert the new item at the end of the linked list.
  PR_INSERT_BEFORE(result, &mDOMMediaQueryLists);

  return result.forget();
}

void
nsDocument::FlushSkinBindings()
{
  BindingManager()->FlushSkinBindings();
}

nsresult
nsDocument::InitializeFrameLoader(nsFrameLoader* aLoader)
{
  mInitializableFrameLoaders.RemoveElement(aLoader);
  // Don't even try to initialize.
  if (mInDestructor) {
    NS_WARNING("Trying to initialize a frame loader while"
               "document is being deleted");
    return NS_ERROR_FAILURE;
  }

  mInitializableFrameLoaders.AppendElement(aLoader);
  if (!mFrameLoaderRunner) {
    mFrameLoaderRunner =
      NewRunnableMethod(this, &nsDocument::MaybeInitializeFinalizeFrameLoaders);
    NS_ENSURE_TRUE(mFrameLoaderRunner, NS_ERROR_OUT_OF_MEMORY);
    nsContentUtils::AddScriptRunner(mFrameLoaderRunner);
  }
  return NS_OK;
}

nsresult
nsDocument::FinalizeFrameLoader(nsFrameLoader* aLoader, nsIRunnable* aFinalizer)
{
  mInitializableFrameLoaders.RemoveElement(aLoader);
  if (mInDestructor) {
    return NS_ERROR_FAILURE;
  }

  mFrameLoaderFinalizers.AppendElement(aFinalizer);
  if (!mFrameLoaderRunner) {
    mFrameLoaderRunner =
      NewRunnableMethod(this, &nsDocument::MaybeInitializeFinalizeFrameLoaders);
    NS_ENSURE_TRUE(mFrameLoaderRunner, NS_ERROR_OUT_OF_MEMORY);
    nsContentUtils::AddScriptRunner(mFrameLoaderRunner);
  }
  return NS_OK;
}

void
nsDocument::MaybeInitializeFinalizeFrameLoaders()
{
  if (mDelayFrameLoaderInitialization || mUpdateNestLevel != 0) {
    // This method will be recalled when mUpdateNestLevel drops to 0,
    // or when !mDelayFrameLoaderInitialization.
    mFrameLoaderRunner = nullptr;
    return;
  }

  // We're not in an update, but it is not safe to run scripts, so
  // postpone frameloader initialization and finalization.
  if (!nsContentUtils::IsSafeToRunScript()) {
    if (!mInDestructor && !mFrameLoaderRunner &&
        (mInitializableFrameLoaders.Length() ||
         mFrameLoaderFinalizers.Length())) {
      mFrameLoaderRunner =
        NewRunnableMethod(this, &nsDocument::MaybeInitializeFinalizeFrameLoaders);
      nsContentUtils::AddScriptRunner(mFrameLoaderRunner);
    }
    return;
  }
  mFrameLoaderRunner = nullptr;

  // Don't use a temporary array for mInitializableFrameLoaders, because
  // loading a frame may cause some other frameloader to be removed from the
  // array. But be careful to keep the loader alive when starting the load!
  while (mInitializableFrameLoaders.Length()) {
    RefPtr<nsFrameLoader> loader = mInitializableFrameLoaders[0];
    mInitializableFrameLoaders.RemoveElementAt(0);
    NS_ASSERTION(loader, "null frameloader in the array?");
    loader->ReallyStartLoading();
  }

  uint32_t length = mFrameLoaderFinalizers.Length();
  if (length > 0) {
    nsTArray<nsCOMPtr<nsIRunnable> > finalizers;
    mFrameLoaderFinalizers.SwapElements(finalizers);
    for (uint32_t i = 0; i < length; ++i) {
      finalizers[i]->Run();
    }
  }
}

void
nsDocument::TryCancelFrameLoaderInitialization(nsIDocShell* aShell)
{
  uint32_t length = mInitializableFrameLoaders.Length();
  for (uint32_t i = 0; i < length; ++i) {
    if (mInitializableFrameLoaders[i]->GetExistingDocShell() == aShell) {
      mInitializableFrameLoaders.RemoveElementAt(i);
      return;
    }
  }
}

nsIDocument*
nsDocument::RequestExternalResource(nsIURI* aURI,
                                    nsINode* aRequestingNode,
                                    ExternalResourceLoad** aPendingLoad)
{
  NS_PRECONDITION(aURI, "Must have a URI");
  NS_PRECONDITION(aRequestingNode, "Must have a node");
  if (mDisplayDocument) {
    return mDisplayDocument->RequestExternalResource(aURI,
                                                     aRequestingNode,
                                                     aPendingLoad);
  }

  return mExternalResourceMap.RequestResource(aURI, aRequestingNode,
                                              this, aPendingLoad);
}

void
nsDocument::EnumerateExternalResources(nsSubDocEnumFunc aCallback, void* aData)
{
  mExternalResourceMap.EnumerateResources(aCallback, aData);
}

nsSMILAnimationController*
nsDocument::GetAnimationController()
{
  // We create the animation controller lazily because most documents won't want
  // one and only SVG documents and the like will call this
  if (mAnimationController)
    return mAnimationController;
  // Refuse to create an Animation Controller for data documents.
  if (mLoadedAsData || mLoadedAsInteractiveData)
    return nullptr;

  mAnimationController = new nsSMILAnimationController(this);

  // If there's a presContext then check the animation mode and pause if
  // necessary.
  nsIPresShell *shell = GetShell();
  if (mAnimationController && shell) {
    nsPresContext *context = shell->GetPresContext();
    if (context &&
        context->ImageAnimationMode() == imgIContainer::kDontAnimMode) {
      mAnimationController->Pause(nsSMILTimeContainer::PAUSE_USERPREF);
    }
  }

  // If we're hidden (or being hidden), notify the newly-created animation
  // controller. (Skip this check for SVG-as-an-image documents, though,
  // because they don't get OnPageShow / OnPageHide calls).
  if (!mIsShowing && !mIsBeingUsedAsImage) {
    mAnimationController->OnPageHide();
  }

  return mAnimationController;
}

PendingAnimationTracker*
nsDocument::GetOrCreatePendingAnimationTracker()
{
  if (!mPendingAnimationTracker) {
    mPendingAnimationTracker = new PendingAnimationTracker(this);
  }

  return mPendingAnimationTracker;
}

/**
 * Retrieve the "direction" property of the document.
 *
 * @lina 01/09/2001
 */
NS_IMETHODIMP
nsDocument::GetDir(nsAString& aDirection)
{
  nsIDocument::GetDir(aDirection);
  return NS_OK;
}

void
nsIDocument::GetDir(nsAString& aDirection) const
{
  aDirection.Truncate();
  Element* rootElement = GetHtmlElement();
  if (rootElement) {
    static_cast<nsGenericHTMLElement*>(rootElement)->GetDir(aDirection);
  }
}

/**
 * Set the "direction" property of the document.
 *
 * @lina 01/09/2001
 */
NS_IMETHODIMP
nsDocument::SetDir(const nsAString& aDirection)
{
  nsIDocument::SetDir(aDirection);
  return NS_OK;
}

void
nsIDocument::SetDir(const nsAString& aDirection)
{
  Element* rootElement = GetHtmlElement();
  if (rootElement) {
    rootElement->SetAttr(kNameSpaceID_None, nsGkAtoms::dir,
                         aDirection, true);
  }
}

NS_IMETHODIMP
nsDocument::GetInputEncoding(nsAString& aInputEncoding)
{
  nsIDocument::GetCharacterSet(aInputEncoding);
  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetMozSyntheticDocument(bool *aSyntheticDocument)
{
  *aSyntheticDocument = mIsSyntheticDocument;
  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetDocumentURI(nsAString& aDocumentURI)
{
  nsString temp;
  nsIDocument::GetDocumentURI(temp);
  aDocumentURI = temp;
  return NS_OK;
}

void
nsIDocument::GetDocumentURI(nsString& aDocumentURI) const
{
  if (mDocumentURI) {
    nsAutoCString uri;
    mDocumentURI->GetSpec(uri);
    CopyUTF8toUTF16(uri, aDocumentURI);
  } else {
    aDocumentURI.Truncate();
  }
}

// Alias of above
NS_IMETHODIMP
nsDocument::GetURL(nsAString& aURL)
{
  return GetDocumentURI(aURL);
}

void
nsIDocument::GetURL(nsString& aURL) const
{
  return GetDocumentURI(aURL);
}

void
nsIDocument::GetDocumentURIFromJS(nsString& aDocumentURI) const
{
  if (!mChromeXHRDocURI || !nsContentUtils::IsCallerChrome()) {
    return GetDocumentURI(aDocumentURI);
  }

  nsAutoCString uri;
  mChromeXHRDocURI->GetSpec(uri);
  CopyUTF8toUTF16(uri, aDocumentURI);
}

nsIURI*
nsIDocument::GetDocumentURIObject() const
{
  if (!mChromeXHRDocURI) {
    return GetDocumentURI();
  }

  return mChromeXHRDocURI;
}


// Returns "BackCompat" if we are in quirks mode, "CSS1Compat" if we are
// in almost standards or full standards mode. See bug 105640.  This was
// implemented to match MSIE's compatMode property.
NS_IMETHODIMP
nsDocument::GetCompatMode(nsAString& aCompatMode)
{
  nsString temp;
  nsIDocument::GetCompatMode(temp);
  aCompatMode = temp;
  return NS_OK;
}

void
nsIDocument::GetCompatMode(nsString& aCompatMode) const
{
  NS_ASSERTION(mCompatMode == eCompatibility_NavQuirks ||
               mCompatMode == eCompatibility_AlmostStandards ||
               mCompatMode == eCompatibility_FullStandards,
               "mCompatMode is neither quirks nor strict for this document");

  if (mCompatMode == eCompatibility_NavQuirks) {
    aCompatMode.AssignLiteral("BackCompat");
  } else {
    aCompatMode.AssignLiteral("CSS1Compat");
  }
}

void
nsDOMAttributeMap::BlastSubtreeToPieces(nsINode *aNode)
{
  if (aNode->IsElement()) {
    Element *element = aNode->AsElement();
    const nsDOMAttributeMap *map = element->GetAttributeMap();
    if (map) {
      nsCOMPtr<nsIAttribute> attr;

      // This non-standard style of iteration is presumably used because some
      // of the code in the loop body can trigger element removal, which
      // invalidates the iterator.
      while (true) {
        auto iter = map->mAttributeCache.ConstIter();
        if (iter.Done()) {
          break;
        }
        nsCOMPtr<nsIAttribute> attr = iter.UserData();
        NS_ASSERTION(attr.get(),
                     "non-nsIAttribute somehow made it into the hashmap?!");

        BlastSubtreeToPieces(attr);

        DebugOnly<nsresult> rv =
          element->UnsetAttr(attr->NodeInfo()->NamespaceID(),
                             attr->NodeInfo()->NameAtom(),
                             false);

        // XXX Should we abort here?
        NS_ASSERTION(NS_SUCCEEDED(rv), "Uh-oh, UnsetAttr shouldn't fail!");
      }
    }
  }

  uint32_t count = aNode->GetChildCount();
  for (uint32_t i = 0; i < count; ++i) {
    BlastSubtreeToPieces(aNode->GetFirstChild());
    aNode->RemoveChildAt(0, false);
  }
}

NS_IMETHODIMP
nsDocument::AdoptNode(nsIDOMNode *aAdoptedNode, nsIDOMNode **aResult)
{
  *aResult = nullptr;

  nsCOMPtr<nsINode> adoptedNode = do_QueryInterface(aAdoptedNode);
  NS_ENSURE_TRUE(adoptedNode, NS_ERROR_UNEXPECTED);

  ErrorResult rv;
  nsINode* result = nsIDocument::AdoptNode(*adoptedNode, rv);
  if (rv.Failed()) {
    return rv.StealNSResult();
  }

  NS_ADDREF(*aResult = result->AsDOMNode());
  return NS_OK;
}

nsINode*
nsIDocument::AdoptNode(nsINode& aAdoptedNode, ErrorResult& rv)
{
  nsINode* adoptedNode = &aAdoptedNode;

  // Scope firing mutation events so that we don't carry any state that
  // might be stale
  {
    nsINode* parent = adoptedNode->GetParentNode();
    if (parent) {
      nsContentUtils::MaybeFireNodeRemoved(adoptedNode, parent,
                                           adoptedNode->OwnerDoc());
    }
  }

  nsAutoScriptBlocker scriptBlocker;

  switch (adoptedNode->NodeType()) {
    case nsIDOMNode::ATTRIBUTE_NODE:
    {
      // Remove from ownerElement.
      RefPtr<Attr> adoptedAttr = static_cast<Attr*>(adoptedNode);

      nsCOMPtr<Element> ownerElement = adoptedAttr->GetOwnerElement(rv);
      if (rv.Failed()) {
        return nullptr;
      }

      if (ownerElement) {
        RefPtr<Attr> newAttr =
          ownerElement->RemoveAttributeNode(*adoptedAttr, rv);
        if (rv.Failed()) {
          return nullptr;
        }

        newAttr.swap(adoptedAttr);
      }

      break;
    }
    case nsIDOMNode::DOCUMENT_FRAGMENT_NODE:
    {
      if (ShadowRoot::FromNode(adoptedNode)) {
        rv.Throw(NS_ERROR_DOM_HIERARCHY_REQUEST_ERR);
        return nullptr;
      }
      MOZ_FALLTHROUGH;
    }
    case nsIDOMNode::ELEMENT_NODE:
    case nsIDOMNode::PROCESSING_INSTRUCTION_NODE:
    case nsIDOMNode::TEXT_NODE:
    case nsIDOMNode::CDATA_SECTION_NODE:
    case nsIDOMNode::COMMENT_NODE:
    case nsIDOMNode::DOCUMENT_TYPE_NODE:
    {
      // Don't allow adopting a node's anonymous subtree out from under it.
      if (adoptedNode->AsContent()->IsRootOfAnonymousSubtree()) {
        rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
        return nullptr;
      }

      // We don't want to adopt an element into its own contentDocument or into
      // a descendant contentDocument, so we check if the frameElement of this
      // document or any of its parents is the adopted node or one of its
      // descendants.
      nsIDocument *doc = this;
      do {
        nsPIDOMWindow *win = doc->GetWindow();
        if (win) {
          nsCOMPtr<nsINode> node = win->GetFrameElementInternal();
          if (node &&
              nsContentUtils::ContentIsDescendantOf(node, adoptedNode)) {
            rv.Throw(NS_ERROR_DOM_HIERARCHY_REQUEST_ERR);
            return nullptr;
          }
        }
      } while ((doc = doc->GetParentDocument()));

      // Remove from parent.
      nsCOMPtr<nsINode> parent = adoptedNode->GetParentNode();
      if (parent) {
        int32_t idx = parent->IndexOf(adoptedNode);
        MOZ_ASSERT(idx >= 0);
        parent->RemoveChildAt(idx, true);
      } else {
        MOZ_ASSERT(!adoptedNode->IsInUncomposedDoc());

        // If we're adopting a node that's not in a document, it might still
        // have a binding applied. Remove the binding from the element now
        // that it's getting adopted into a new document.
        // TODO Fully tear down the binding.
        adoptedNode->AsContent()->SetXBLBinding(nullptr);
      }

      break;
    }
    case nsIDOMNode::DOCUMENT_NODE:
    {
      rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
      return nullptr;
    }
    default:
    {
      NS_WARNING("Don't know how to adopt this nodetype for adoptNode.");

      rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
      return nullptr;
    }
  }

  nsCOMPtr<nsIDocument> oldDocument = adoptedNode->OwnerDoc();
  bool sameDocument = oldDocument == this;

  AutoJSContext cx;
  JS::Rooted<JSObject*> newScope(cx, nullptr);
  if (!sameDocument) {
    newScope = GetWrapper();
    if (!newScope && GetScopeObject() && GetScopeObject()->GetGlobalJSObject()) {
      // Make sure cx is in a semi-sane compartment before we call WrapNative.
      // It's kind of irrelevant, given that we're passing aAllowWrapping =
      // false, and documents should always insist on being wrapped in an
      // canonical scope. But we try to pass something sane anyway.
      JSAutoCompartment ac(cx, GetScopeObject()->GetGlobalJSObject());
      JS::Rooted<JS::Value> v(cx);
      rv = nsContentUtils::WrapNative(cx, this, this, &v,
                                      /* aAllowWrapping = */ false);
      if (rv.Failed())
        return nullptr;
      newScope = &v.toObject();
    }
  }

  nsCOMArray<nsINode> nodesWithProperties;
  rv = nsNodeUtils::Adopt(adoptedNode, sameDocument ? nullptr : mNodeInfoManager,
                          newScope, nodesWithProperties);
  if (rv.Failed()) {
    // Disconnect all nodes from their parents, since some have the old document
    // as their ownerDocument and some have this as their ownerDocument.
    nsDOMAttributeMap::BlastSubtreeToPieces(adoptedNode);

    if (!sameDocument && oldDocument) {
      uint32_t count = nodesWithProperties.Count();
      for (uint32_t j = 0; j < oldDocument->GetPropertyTableCount(); ++j) {
        for (uint32_t i = 0; i < count; ++i) {
          // Remove all properties.
          oldDocument->PropertyTable(j)->
            DeleteAllPropertiesFor(nodesWithProperties[i]);
        }
      }
    }

    return nullptr;
  }

  uint32_t count = nodesWithProperties.Count();
  if (!sameDocument && oldDocument) {
    for (uint32_t j = 0; j < oldDocument->GetPropertyTableCount(); ++j) {
      nsPropertyTable *oldTable = oldDocument->PropertyTable(j);
      nsPropertyTable *newTable = PropertyTable(j);
      for (uint32_t i = 0; i < count; ++i) {
        rv = oldTable->TransferOrDeleteAllPropertiesFor(nodesWithProperties[i],
                                                        newTable);
      }
    }

    if (rv.Failed()) {
      // Disconnect all nodes from their parents.
      nsDOMAttributeMap::BlastSubtreeToPieces(adoptedNode);

      return nullptr;
    }
  }

  NS_ASSERTION(adoptedNode->OwnerDoc() == this,
               "Should still be in the document we just got adopted into");

  return adoptedNode;
}

nsViewportInfo
nsDocument::GetViewportInfo(const ScreenIntSize& aDisplaySize)
{
  // Compute the CSS-to-LayoutDevice pixel scale as the product of the
  // widget scale and the full zoom.
  nsPresContext* context = mPresShell->GetPresContext();
  float fullZoom = context ? context->GetFullZoom() : 1.0;
  fullZoom = (fullZoom == 0.0) ? 1.0 : fullZoom;
  CSSToLayoutDeviceScale layoutDeviceScale = context ? context->CSSToDevPixelScale() : CSSToLayoutDeviceScale(1);

  CSSToScreenScale defaultScale = layoutDeviceScale
                                * LayoutDeviceToScreenScale(1.0);

  // Special behaviour for desktop mode, provided we are not on an about: page
  nsPIDOMWindow* win = GetWindow();
  if (win && win->IsDesktopModeViewport() && !IsAboutPage())
  {
    CSSCoord viewportWidth = gfxPrefs::DesktopViewportWidth() / fullZoom;
    CSSToScreenScale scaleToFit(aDisplaySize.width / viewportWidth);
    float aspectRatio = (float)aDisplaySize.height / aDisplaySize.width;
    CSSSize viewportSize(viewportWidth, viewportWidth * aspectRatio);
    ScreenIntSize fakeDesktopSize = RoundedToInt(viewportSize * scaleToFit);
    return nsViewportInfo(fakeDesktopSize,
                          scaleToFit,
                          /*allowZoom*/ true);
  }

  if (!gfxPrefs::MetaViewportEnabled()) {
    return nsViewportInfo(aDisplaySize,
                          defaultScale,
                          /*allowZoom*/ false);
  }

  // In cases where the width of the CSS viewport is less than or equal to the width
  // of the display (i.e. width <= device-width) then we disable double-tap-to-zoom
  // behaviour. See bug 941995 for details.

  switch (mViewportType) {
  case DisplayWidthHeight:
    return nsViewportInfo(aDisplaySize,
                          defaultScale,
                          /*allowZoom*/ true);
  case Unknown:
  {
    nsAutoString viewport;
    GetHeaderData(nsGkAtoms::viewport, viewport);
    if (viewport.IsEmpty()) {
      // If the docType specifies that we are on a site optimized for mobile,
      // then we want to return specially crafted defaults for the viewport info.
      nsCOMPtr<nsIDOMDocumentType> docType;
      nsresult rv = GetDoctype(getter_AddRefs(docType));
      if (NS_SUCCEEDED(rv) && docType) {
        nsAutoString docId;
        rv = docType->GetPublicId(docId);
        if (NS_SUCCEEDED(rv)) {
          if ((docId.Find("WAP") != -1) ||
              (docId.Find("Mobile") != -1) ||
              (docId.Find("WML") != -1))
          {
            // We're making an assumption that the docType can't change here
            mViewportType = DisplayWidthHeight;
            return nsViewportInfo(aDisplaySize,
                                  defaultScale,
                                  /*allowZoom*/true);
          }
        }
      }

      nsAutoString handheldFriendly;
      GetHeaderData(nsGkAtoms::handheldFriendly, handheldFriendly);
      if (handheldFriendly.EqualsLiteral("true")) {
        mViewportType = DisplayWidthHeight;
        return nsViewportInfo(aDisplaySize,
                              defaultScale,
                              /*allowZoom*/true);
      }
    }

    nsAutoString minScaleStr;
    GetHeaderData(nsGkAtoms::viewport_minimum_scale, minScaleStr);

    nsresult errorCode;
    mScaleMinFloat = LayoutDeviceToScreenScale(minScaleStr.ToFloat(&errorCode));

    if (NS_FAILED(errorCode)) {
      mScaleMinFloat = kViewportMinScale;
    }

    mScaleMinFloat = mozilla::clamped(
        mScaleMinFloat, kViewportMinScale, kViewportMaxScale);

    nsAutoString maxScaleStr;
    GetHeaderData(nsGkAtoms::viewport_maximum_scale, maxScaleStr);

    // We define a special error code variable for the scale and max scale,
    // because they are used later (see the width calculations).
    nsresult scaleMaxErrorCode;
    mScaleMaxFloat = LayoutDeviceToScreenScale(maxScaleStr.ToFloat(&scaleMaxErrorCode));

    if (NS_FAILED(scaleMaxErrorCode)) {
      mScaleMaxFloat = kViewportMaxScale;
    }

    mScaleMaxFloat = mozilla::clamped(
        mScaleMaxFloat, kViewportMinScale, kViewportMaxScale);

    nsAutoString scaleStr;
    GetHeaderData(nsGkAtoms::viewport_initial_scale, scaleStr);

    nsresult scaleErrorCode;
    mScaleFloat = LayoutDeviceToScreenScale(scaleStr.ToFloat(&scaleErrorCode));

    nsAutoString widthStr, heightStr;

    GetHeaderData(nsGkAtoms::viewport_height, heightStr);
    GetHeaderData(nsGkAtoms::viewport_width, widthStr);

    mAutoSize = false;

    if (widthStr.EqualsLiteral("device-width")) {
      mAutoSize = true;
    }

    if (widthStr.IsEmpty() &&
        (heightStr.EqualsLiteral("device-height") ||
         (mScaleFloat.scale == 1.0)))
    {
      mAutoSize = true;
    }

    nsresult widthErrorCode, heightErrorCode;
    mViewportSize.width = widthStr.ToInteger(&widthErrorCode);
    mViewportSize.height = heightStr.ToInteger(&heightErrorCode);

    // If width or height has not been set to a valid number by this point,
    // fall back to a default value.
    mValidWidth = (!widthStr.IsEmpty() && NS_SUCCEEDED(widthErrorCode) && mViewportSize.width > 0);
    mValidHeight = (!heightStr.IsEmpty() && NS_SUCCEEDED(heightErrorCode) && mViewportSize.height > 0);

    mAllowZoom = true;
    nsAutoString userScalable;
    GetHeaderData(nsGkAtoms::viewport_user_scalable, userScalable);

    if ((userScalable.EqualsLiteral("0")) ||
        (userScalable.EqualsLiteral("no")) ||
        (userScalable.EqualsLiteral("false"))) {
      mAllowZoom = false;
    }

    mScaleStrEmpty = scaleStr.IsEmpty();
    mWidthStrEmpty = widthStr.IsEmpty();
    mValidScaleFloat = !scaleStr.IsEmpty() && NS_SUCCEEDED(scaleErrorCode);
    mValidMaxScale = !maxScaleStr.IsEmpty() && NS_SUCCEEDED(scaleMaxErrorCode);

    mViewportType = Specified;
    MOZ_FALLTHROUGH;
  }
  case Specified:
  default:
    LayoutDeviceToScreenScale effectiveMinScale = mScaleMinFloat;
    LayoutDeviceToScreenScale effectiveMaxScale = mScaleMaxFloat;
    bool effectiveValidMaxScale = mValidMaxScale;
    bool effectiveAllowZoom = mAllowZoom;
    if (gfxPrefs::ForceUserScalable()) {
      // If the pref to force user-scalable is enabled, we ignore the values
      // from the meta-viewport tag for these properties and just assume they
      // allow the page to be scalable. Note in particular that this code is
      // in the "Specified" branch of the enclosing switch statement, so that
      // calls to GetViewportInfo always use the latest value of the
      // ForceUserScalable pref. Other codepaths that return nsViewportInfo
      // instances are all consistent with ForceUserScalable() already.
      effectiveMinScale = kViewportMinScale;
      effectiveMaxScale = kViewportMaxScale;
      effectiveValidMaxScale = true;
      effectiveAllowZoom = true;
    }

    CSSSize size = mViewportSize;

    if (!mValidWidth) {
      if (mValidHeight && !aDisplaySize.IsEmpty()) {
        size.width = size.height * aDisplaySize.width / aDisplaySize.height;
      } else {
        // Stretch CSS pixel size of viewport to keep device pixel size
        // unchanged after full zoom applied.
        // See bug 974242.
        size.width = gfxPrefs::DesktopViewportWidth() / fullZoom;
      }
    }

    if (!mValidHeight) {
      if (!aDisplaySize.IsEmpty()) {
        size.height = size.width * aDisplaySize.height / aDisplaySize.width;
      } else {
        size.height = size.width;
      }
    }

    CSSToScreenScale scaleFloat = mScaleFloat * layoutDeviceScale;
    CSSToScreenScale scaleMinFloat = effectiveMinScale * layoutDeviceScale;
    CSSToScreenScale scaleMaxFloat = effectiveMaxScale * layoutDeviceScale;

    if (mAutoSize) {
      // aDisplaySize is in screen pixels; convert them to CSS pixels for the viewport size.
      CSSToScreenScale defaultPixelScale = layoutDeviceScale * LayoutDeviceToScreenScale(1.0f);
      size = ScreenSize(aDisplaySize) / defaultPixelScale;
    }

    size.width = clamped(size.width, float(kViewportMinSize.width), float(kViewportMaxSize.width));

    // Also recalculate the default zoom, if it wasn't specified in the metadata,
    // and the width is specified.
    if (mScaleStrEmpty && !mWidthStrEmpty) {
      CSSToScreenScale defaultScale(float(aDisplaySize.width) / size.width);
      scaleFloat = (scaleFloat > defaultScale) ? scaleFloat : defaultScale;
    }

    size.height = clamped(size.height, float(kViewportMinSize.height), float(kViewportMaxSize.height));

    // We need to perform a conversion, but only if the initial or maximum
    // scale were set explicitly by the user.
    if (mValidScaleFloat && scaleFloat >= scaleMinFloat && scaleFloat <= scaleMaxFloat) {
      CSSSize displaySize = ScreenSize(aDisplaySize) / scaleFloat;
      size.width = std::max(size.width, displaySize.width);
      size.height = std::max(size.height, displaySize.height);
    } else if (effectiveValidMaxScale) {
      CSSSize displaySize = ScreenSize(aDisplaySize) / scaleMaxFloat;
      size.width = std::max(size.width, displaySize.width);
      size.height = std::max(size.height, displaySize.height);
    }

    return nsViewportInfo(scaleFloat, scaleMinFloat, scaleMaxFloat, size,
                          mAutoSize, effectiveAllowZoom);
  }
}

EventListenerManager*
nsDocument::GetOrCreateListenerManager()
{
  if (!mListenerManager) {
    mListenerManager =
      new EventListenerManager(static_cast<EventTarget*>(this));
    SetFlags(NODE_HAS_LISTENERMANAGER);
  }

  return mListenerManager;
}

EventListenerManager*
nsDocument::GetExistingListenerManager() const
{
  return mListenerManager;
}

nsresult
nsDocument::PreHandleEvent(EventChainPreVisitor& aVisitor)
{
  aVisitor.mCanHandle = true;
   // FIXME! This is a hack to make middle mouse paste working also in Editor.
   // Bug 329119
  aVisitor.mForceContentDispatch = true;

  // Load events must not propagate to |window| object, see bug 335251.
  if (aVisitor.mEvent->mMessage != eLoad) {
    nsGlobalWindow* window = static_cast<nsGlobalWindow*>(GetWindow());
    aVisitor.mParentTarget =
      window ? window->GetTargetForEventTargetChain() : nullptr;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDocument::CreateEvent(const nsAString& aEventType, nsIDOMEvent** aReturn)
{
  NS_ENSURE_ARG_POINTER(aReturn);
  ErrorResult rv;
  *aReturn = nsIDocument::CreateEvent(aEventType, rv).take();
  return rv.StealNSResult();
}

already_AddRefed<Event>
nsIDocument::CreateEvent(const nsAString& aEventType, ErrorResult& rv) const
{
  nsIPresShell *shell = GetShell();

  nsPresContext *presContext = nullptr;

  if (shell) {
    // Retrieve the context
    presContext = shell->GetPresContext();
  }

  // Create event even without presContext.
  RefPtr<Event> ev =
    EventDispatcher::CreateEvent(const_cast<nsIDocument*>(this), presContext,
                                 nullptr, aEventType);
  if (!ev) {
    rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return nullptr;
  }
  WidgetEvent* e = ev->WidgetEventPtr();
  e->mFlags.mBubbles = false;
  e->mFlags.mCancelable = false;
  return ev.forget();
}

void
nsDocument::FlushPendingNotifications(mozFlushType aType)
{
  nsDocumentOnStack dos(this);

  // We need to flush the sink for non-HTML documents (because the XML
  // parser still does insertion with deferred notifications).  We
  // also need to flush the sink if this is a layout-related flush, to
  // make sure that layout is started as needed.  But we can skip that
  // part if we have no presshell or if it's already done an initial
  // reflow.
  if ((!IsHTMLDocument() ||
       (aType > Flush_ContentAndNotify && mPresShell &&
        !mPresShell->DidInitialize())) &&
      (mParser || mWeakSink)) {
    nsCOMPtr<nsIContentSink> sink;
    if (mParser) {
      sink = mParser->GetContentSink();
    } else {
      sink = do_QueryReferent(mWeakSink);
      if (!sink) {
        mWeakSink = nullptr;
      }
    }
    // Determine if it is safe to flush the sink notifications
    // by determining if it safe to flush all the presshells.
    if (sink && (aType == Flush_Content || IsSafeToFlush())) {
      sink->FlushPendingNotifications(aType);
    }
  }

  // Should we be flushing pending binding constructors in here?

  if (aType <= Flush_ContentAndNotify) {
    // Nothing to do here
    return;
  }

  // If we have a parent we must flush the parent too to ensure that our
  // container is reflowed if its size was changed.  But if it's not safe to
  // flush ourselves, then don't flush the parent, since that can cause things
  // like resizes of our frame's widget, which we can't handle while flushing
  // is unsafe.
  // Since media queries mean that a size change of our container can
  // affect style, we need to promote a style flush on ourself to a
  // layout flush on our parent, since we need our container to be the
  // correct size to determine the correct style.
  if (mParentDocument && IsSafeToFlush()) {
    mozFlushType parentType = aType;
    if (aType >= Flush_Style)
      parentType = std::max(Flush_Layout, aType);
    mParentDocument->FlushPendingNotifications(parentType);
  }

  // We can optimize away getting our presshell and calling
  // FlushPendingNotifications on it if we don't need a flush of the sort we're
  // looking at.  The one exception is if mInFlush is true, because in that
  // case we might have set mNeedStyleFlush and mNeedLayoutFlush to false
  // already but the presshell hasn't actually done the corresponding work yet.
  // So if mInFlush and reentering this code, we need to flush the presshell.
  if (mNeedStyleFlush ||
      (mNeedLayoutFlush && aType >= Flush_InterruptibleLayout) ||
      aType >= Flush_Display ||
      mInFlush) {
    nsCOMPtr<nsIPresShell> shell = GetShell();
    if (shell) {
      mNeedStyleFlush = false;
      mNeedLayoutFlush = mNeedLayoutFlush && (aType < Flush_InterruptibleLayout);
      // mInFlush is a bitfield, so can't us AutoRestore here.  But we
      // need to keep track of multi-level reentry correctly, so need
      // to restore the old mInFlush value.
      bool oldInFlush = mInFlush;
      mInFlush = true;
      shell->FlushPendingNotifications(aType);
      mInFlush = oldInFlush;
    }
  }
}

static bool
Copy(nsIDocument* aDocument, void* aData)
{
  nsTArray<nsCOMPtr<nsIDocument> >* resources =
    static_cast<nsTArray<nsCOMPtr<nsIDocument> >* >(aData);
  resources->AppendElement(aDocument);
  return true;
}

void
nsDocument::FlushExternalResources(mozFlushType aType)
{
  NS_ASSERTION(aType >= Flush_Style,
    "should only need to flush for style or higher in external resources");
  if (GetDisplayDocument()) {
    return;
  }
  nsTArray<nsCOMPtr<nsIDocument> > resources;
  EnumerateExternalResources(Copy, &resources);

  for (uint32_t i = 0; i < resources.Length(); i++) {
    resources[i]->FlushPendingNotifications(aType);
  }
}

void
nsDocument::SetXMLDeclaration(const char16_t *aVersion,
                              const char16_t *aEncoding,
                              const int32_t aStandalone)
{
  if (!aVersion || *aVersion == '\0') {
    mXMLDeclarationBits = 0;
    return;
  }

  mXMLDeclarationBits = XML_DECLARATION_BITS_DECLARATION_EXISTS;

  if (aEncoding && *aEncoding != '\0') {
    mXMLDeclarationBits |= XML_DECLARATION_BITS_ENCODING_EXISTS;
  }

  if (aStandalone == 1) {
    mXMLDeclarationBits |= XML_DECLARATION_BITS_STANDALONE_EXISTS |
                           XML_DECLARATION_BITS_STANDALONE_YES;
  }
  else if (aStandalone == 0) {
    mXMLDeclarationBits |= XML_DECLARATION_BITS_STANDALONE_EXISTS;
  }
}

void
nsDocument::GetXMLDeclaration(nsAString& aVersion, nsAString& aEncoding,
                              nsAString& aStandalone)
{
  aVersion.Truncate();
  aEncoding.Truncate();
  aStandalone.Truncate();

  if (!(mXMLDeclarationBits & XML_DECLARATION_BITS_DECLARATION_EXISTS)) {
    return;
  }

  // always until we start supporting 1.1 etc.
  aVersion.AssignLiteral("1.0");

  if (mXMLDeclarationBits & XML_DECLARATION_BITS_ENCODING_EXISTS) {
    // This is what we have stored, not necessarily what was written
    // in the original
    GetCharacterSet(aEncoding);
  }

  if (mXMLDeclarationBits & XML_DECLARATION_BITS_STANDALONE_EXISTS) {
    if (mXMLDeclarationBits & XML_DECLARATION_BITS_STANDALONE_YES) {
      aStandalone.AssignLiteral("yes");
    } else {
      aStandalone.AssignLiteral("no");
    }
  }
}

bool
nsDocument::IsScriptEnabled()
{
  // If this document is sandboxed without 'allow-scripts'
  // script is not enabled
  if (HasScriptsBlockedBySandbox()) {
    return false;
  }

  nsCOMPtr<nsIScriptSecurityManager> sm(do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID));
  NS_ENSURE_TRUE(sm, false);

  nsCOMPtr<nsIScriptGlobalObject> globalObject = do_QueryInterface(GetInnerWindow());
  if (!globalObject && mMasterDocument) {
    globalObject = do_QueryInterface(mMasterDocument->GetInnerWindow());
  }
  if (!globalObject || !globalObject->GetGlobalJSObject()) {
    return false;
  }

  return sm->ScriptAllowed(globalObject->GetGlobalJSObject());
}

nsRadioGroupStruct*
nsDocument::GetRadioGroupInternal(const nsAString& aName) const
{
#ifdef DEBUG
  if (IsHTMLDocument()) {
    nsAutoString lcName;
    ToLowerCase(aName, lcName);
    MOZ_ASSERT(aName == lcName);
  }
#endif

  nsRadioGroupStruct* radioGroup;
  if (!mRadioGroups.Get(aName, &radioGroup)) {
    return nullptr;
  }

  return radioGroup;
}

nsRadioGroupStruct*
nsDocument::GetRadioGroup(const nsAString& aName) const
{
  nsAutoString tmKey(aName);
  if (IsHTMLDocument()) {
    ToLowerCase(tmKey); //should case-insensitive.
  }

  return GetRadioGroupInternal(tmKey);
}

nsRadioGroupStruct*
nsDocument::GetOrCreateRadioGroup(const nsAString& aName)
{
  nsAutoString tmKey(aName);
  if (IsHTMLDocument()) {
    ToLowerCase(tmKey); //should case-insensitive.
  }

  if (nsRadioGroupStruct* radioGroup = GetRadioGroupInternal(tmKey)) {
    return radioGroup;
  }

  nsAutoPtr<nsRadioGroupStruct> newRadioGroup(new nsRadioGroupStruct());
  mRadioGroups.Put(tmKey, newRadioGroup);

  return newRadioGroup.forget();
}

void
nsDocument::SetCurrentRadioButton(const nsAString& aName,
                                  HTMLInputElement* aRadio)
{
  nsRadioGroupStruct* radioGroup = GetOrCreateRadioGroup(aName);
  radioGroup->mSelectedRadioButton = aRadio;
}

HTMLInputElement*
nsDocument::GetCurrentRadioButton(const nsAString& aName)
{
  return GetOrCreateRadioGroup(aName)->mSelectedRadioButton;
}

NS_IMETHODIMP
nsDocument::GetNextRadioButton(const nsAString& aName,
                               const bool aPrevious,
                               HTMLInputElement* aFocusedRadio,
                               HTMLInputElement** aRadioOut)
{
  // XXX Can we combine the HTML radio button method impls of
  //     nsDocument and nsHTMLFormControl?
  // XXX Why is HTML radio button stuff in nsDocument, as
  //     opposed to nsHTMLDocument?
  *aRadioOut = nullptr;

  nsRadioGroupStruct* radioGroup = GetOrCreateRadioGroup(aName);

  // Return the radio button relative to the focused radio button.
  // If no radio is focused, get the radio relative to the selected one.
  RefPtr<HTMLInputElement> currentRadio;
  if (aFocusedRadio) {
    currentRadio = aFocusedRadio;
  }
  else {
    currentRadio = radioGroup->mSelectedRadioButton;
    if (!currentRadio) {
      return NS_ERROR_FAILURE;
    }
  }
  int32_t index = radioGroup->mRadioButtons.IndexOf(currentRadio);
  if (index < 0) {
    return NS_ERROR_FAILURE;
  }

  int32_t numRadios = radioGroup->mRadioButtons.Count();
  RefPtr<HTMLInputElement> radio;
  do {
    if (aPrevious) {
      if (--index < 0) {
        index = numRadios -1;
      }
    }
    else if (++index >= numRadios) {
      index = 0;
    }
    NS_ASSERTION(static_cast<nsGenericHTMLFormElement*>(radioGroup->mRadioButtons[index])->IsHTMLElement(nsGkAtoms::input),
                 "mRadioButtons holding a non-radio button");
    radio = static_cast<HTMLInputElement*>(radioGroup->mRadioButtons[index]);
  } while (radio->Disabled() && radio != currentRadio);

  radio.forget(aRadioOut);
  return NS_OK;
}

void
nsDocument::AddToRadioGroup(const nsAString& aName,
                            nsIFormControl* aRadio)
{
  nsRadioGroupStruct* radioGroup = GetOrCreateRadioGroup(aName);
  radioGroup->mRadioButtons.AppendObject(aRadio);

  nsCOMPtr<nsIContent> element = do_QueryInterface(aRadio);
  NS_ASSERTION(element, "radio controls have to be content elements");
  if (element->HasAttr(kNameSpaceID_None, nsGkAtoms::required)) {
    radioGroup->mRequiredRadioCount++;
  }
}

void
nsDocument::RemoveFromRadioGroup(const nsAString& aName,
                                 nsIFormControl* aRadio)
{
  nsRadioGroupStruct* radioGroup = GetOrCreateRadioGroup(aName);
  radioGroup->mRadioButtons.RemoveObject(aRadio);

  nsCOMPtr<nsIContent> element = do_QueryInterface(aRadio);
  NS_ASSERTION(element, "radio controls have to be content elements");
  if (element->HasAttr(kNameSpaceID_None, nsGkAtoms::required)) {
    NS_ASSERTION(radioGroup->mRequiredRadioCount != 0,
                 "mRequiredRadioCount about to wrap below 0!");
    radioGroup->mRequiredRadioCount--;
  }
}

NS_IMETHODIMP
nsDocument::WalkRadioGroup(const nsAString& aName,
                           nsIRadioVisitor* aVisitor,
                           bool aFlushContent)
{
  nsRadioGroupStruct* radioGroup = GetOrCreateRadioGroup(aName);

  for (int i = 0; i < radioGroup->mRadioButtons.Count(); i++) {
    if (!aVisitor->Visit(radioGroup->mRadioButtons[i])) {
      return NS_OK;
    }
  }

  return NS_OK;
}

uint32_t
nsDocument::GetRequiredRadioCount(const nsAString& aName) const
{
  nsRadioGroupStruct* radioGroup = GetRadioGroup(aName);
  return radioGroup ? radioGroup->mRequiredRadioCount : 0;
}

void
nsDocument::RadioRequiredWillChange(const nsAString& aName, bool aRequiredAdded)
{
  nsRadioGroupStruct* radioGroup = GetOrCreateRadioGroup(aName);

  if (aRequiredAdded) {
    radioGroup->mRequiredRadioCount++;
  } else {
    NS_ASSERTION(radioGroup->mRequiredRadioCount != 0,
                 "mRequiredRadioCount about to wrap below 0!");
    radioGroup->mRequiredRadioCount--;
  }
}

bool
nsDocument::GetValueMissingState(const nsAString& aName) const
{
  nsRadioGroupStruct* radioGroup = GetRadioGroup(aName);
  return radioGroup && radioGroup->mGroupSuffersFromValueMissing;
}

void
nsDocument::SetValueMissingState(const nsAString& aName, bool aValue)
{
  nsRadioGroupStruct* radioGroup = GetOrCreateRadioGroup(aName);
  radioGroup->mGroupSuffersFromValueMissing = aValue;
}

void
nsDocument::RetrieveRelevantHeaders(nsIChannel *aChannel)
{
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);
  PRTime modDate = 0;
  nsresult rv;

  if (httpChannel) {
    nsAutoCString tmp;
    rv = httpChannel->GetResponseHeader(NS_LITERAL_CSTRING("last-modified"),
                                        tmp);

    if (NS_SUCCEEDED(rv)) {
      PRTime time;
      PRStatus st = PR_ParseTimeString(tmp.get(), true, &time);
      if (st == PR_SUCCESS) {
        modDate = time;
      }
    }

    // The misspelled key 'referer' is as per the HTTP spec
    rv = httpChannel->GetRequestHeader(NS_LITERAL_CSTRING("referer"),
                                       mReferrer);

    static const char *const headers[] = {
      "default-style",
      "content-style-type",
      "content-language",
      "content-disposition",
      "refresh",
      "x-dns-prefetch-control",
      "x-frame-options",
      // add more http headers if you need
      // XXXbz don't add content-location support without reading bug
      // 238654 and its dependencies/dups first.
      0
    };

    nsAutoCString headerVal;
    const char *const *name = headers;
    while (*name) {
      rv =
        httpChannel->GetResponseHeader(nsDependentCString(*name), headerVal);
      if (NS_SUCCEEDED(rv) && !headerVal.IsEmpty()) {
        nsCOMPtr<nsIAtom> key = NS_Atomize(*name);
        SetHeaderData(key, NS_ConvertASCIItoUTF16(headerVal));
      }
      ++name;
    }
  } else {
    nsCOMPtr<nsIFileChannel> fileChannel = do_QueryInterface(aChannel);
    if (fileChannel) {
      nsCOMPtr<nsIFile> file;
      fileChannel->GetFile(getter_AddRefs(file));
      if (file) {
        PRTime msecs;
        rv = file->GetLastModifiedTime(&msecs);

        if (NS_SUCCEEDED(rv)) {
          modDate = msecs * int64_t(PR_USEC_PER_MSEC);
        }
      }
    } else {
      nsAutoCString contentDisp;
      rv = aChannel->GetContentDispositionHeader(contentDisp);
      if (NS_SUCCEEDED(rv)) {
        SetHeaderData(nsGkAtoms::headerContentDisposition,
                      NS_ConvertASCIItoUTF16(contentDisp));
      }
    }
  }

  mLastModified.Truncate();
  if (modDate != 0) {
    GetFormattedTimeString(modDate, mLastModified);
  }
}

already_AddRefed<Element>
nsDocument::CreateElem(const nsAString& aName, nsIAtom *aPrefix,
                       int32_t aNamespaceID)
{
#ifdef DEBUG
  nsAutoString qName;
  if (aPrefix) {
    aPrefix->ToString(qName);
    qName.Append(':');
  }
  qName.Append(aName);

  // Note: "a:b:c" is a valid name in non-namespaces XML, and
  // nsDocument::CreateElement can call us with such a name and no prefix,
  // which would cause an error if we just used true here.
  bool nsAware = aPrefix != nullptr || aNamespaceID != GetDefaultNamespaceID();
  NS_ASSERTION(NS_SUCCEEDED(nsContentUtils::CheckQName(qName, nsAware)),
               "Don't pass invalid prefixes to nsDocument::CreateElem, "
               "check caller.");
#endif

  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  mNodeInfoManager->GetNodeInfo(aName, aPrefix, aNamespaceID,
                                nsIDOMNode::ELEMENT_NODE,
                                getter_AddRefs(nodeInfo));
  NS_ENSURE_TRUE(nodeInfo, nullptr);

  nsCOMPtr<Element> element;
  nsresult rv = NS_NewElement(getter_AddRefs(element), nodeInfo.forget(),
                              NOT_FROM_PARSER);
  return NS_SUCCEEDED(rv) ? element.forget() : nullptr;
}

bool
nsDocument::IsSafeToFlush() const
{
  nsIPresShell* shell = GetShell();
  if (!shell)
    return true;

  return shell->IsSafeToFlush();
}

void
nsDocument::Sanitize()
{
  // Sanitize the document by resetting all password fields and any form
  // fields with autocomplete=off to their default values.  We do this now,
  // instead of when the presentation is restored, to offer some protection
  // in case there is ever an exploit that allows a cached document to be
  // accessed from a different document.

  // First locate all input elements, regardless of whether they are
  // in a form, and reset the password and autocomplete=off elements.

  RefPtr<nsContentList> nodes = GetElementsByTagName(NS_LITERAL_STRING("input"));

  nsCOMPtr<nsIContent> item;
  nsAutoString value;

  uint32_t length = nodes->Length(true);
  for (uint32_t i = 0; i < length; ++i) {
    NS_ASSERTION(nodes->Item(i), "null item in node list!");

    RefPtr<HTMLInputElement> input = HTMLInputElement::FromContentOrNull(nodes->Item(i));
    if (!input)
      continue;

    bool resetValue = false;

    input->GetAttribute(NS_LITERAL_STRING("autocomplete"), value);
    if (value.LowerCaseEqualsLiteral("off")) {
      resetValue = true;
    } else {
      input->GetType(value);
      if (value.LowerCaseEqualsLiteral("password"))
        resetValue = true;
    }

    if (resetValue) {
      input->Reset();
    }
  }

  // Now locate all _form_ elements that have autocomplete=off and reset them
  nodes = GetElementsByTagName(NS_LITERAL_STRING("form"));

  length = nodes->Length(true);
  for (uint32_t i = 0; i < length; ++i) {
    NS_ASSERTION(nodes->Item(i), "null item in nodelist");

    nsCOMPtr<nsIDOMHTMLFormElement> form = do_QueryInterface(nodes->Item(i));
    if (!form)
      continue;

    nodes->Item(i)->AsElement()->GetAttr(kNameSpaceID_None,
                                         nsGkAtoms::autocomplete, value);
    if (value.LowerCaseEqualsLiteral("off"))
      form->Reset();
  }
}

struct SubDocEnumArgs
{
  nsIDocument::nsSubDocEnumFunc callback;
  void *data;
};

void
nsDocument::EnumerateSubDocuments(nsSubDocEnumFunc aCallback, void *aData)
{
  if (!mSubDocuments) {
    return;
  }

  for (auto iter = mSubDocuments->Iter(); !iter.Done(); iter.Next()) {
    auto entry = static_cast<SubDocMapEntry*>(iter.Get());
    nsIDocument* subdoc = entry->mSubDocument;
    bool next = subdoc ? aCallback(subdoc, aData) : true;
    if (!next) {
      break;
    }
  }
}

#ifdef DEBUG_bryner
#define DEBUG_PAGE_CACHE
#endif

bool
nsDocument::CanSavePresentation(nsIRequest *aNewRequest)
{
  if (EventHandlingSuppressed()) {
    return false;
  }

  nsPIDOMWindow* win = GetInnerWindow();
  if (win && win->TimeoutSuspendCount()) {
    return false;
  }

  // Check our event listener manager for unload/beforeunload listeners.
  nsCOMPtr<EventTarget> piTarget = do_QueryInterface(mScriptGlobalObject);
  if (piTarget) {
    EventListenerManager* manager = piTarget->GetExistingListenerManager();
    if (manager && manager->HasUnloadListeners()) {
      return false;
    }
  }

  // Check if we have pending network requests
  nsCOMPtr<nsILoadGroup> loadGroup = GetDocumentLoadGroup();
  if (loadGroup) {
    nsCOMPtr<nsISimpleEnumerator> requests;
    loadGroup->GetRequests(getter_AddRefs(requests));

    bool hasMore = false;

    // We want to bail out if we have any requests other than aNewRequest (or
    // in the case when aNewRequest is a part of a multipart response the base
    // channel the multipart response is coming in on).
    nsCOMPtr<nsIChannel> baseChannel;
    nsCOMPtr<nsIMultiPartChannel> part(do_QueryInterface(aNewRequest));
    if (part) {
      part->GetBaseChannel(getter_AddRefs(baseChannel));
    }

    while (NS_SUCCEEDED(requests->HasMoreElements(&hasMore)) && hasMore) {
      nsCOMPtr<nsISupports> elem;
      requests->GetNext(getter_AddRefs(elem));

      nsCOMPtr<nsIRequest> request = do_QueryInterface(elem);
      if (request && request != aNewRequest && request != baseChannel) {
#ifdef DEBUG_PAGE_CACHE
        nsAutoCString requestName, docSpec;
        request->GetName(requestName);
        if (mDocumentURI)
          mDocumentURI->GetSpec(docSpec);

        printf("document %s has request %s\n",
               docSpec.get(), requestName.get());
#endif
        return false;
      }
    }
  }

  // Check if we have active GetUserMedia use
  if (MediaManager::Exists() && win &&
      MediaManager::Get()->IsWindowStillActive(win->WindowID())) {
    return false;
  }

#ifdef MOZ_WEBRTC
  // Check if we have active PeerConnections
  nsCOMPtr<IPeerConnectionManager> pcManager =
    do_GetService(IPEERCONNECTION_MANAGER_CONTRACTID);

  if (pcManager && win) {
    bool active;
    pcManager->HasActivePeerConnection(win->WindowID(), &active);
    if (active) {
      return false;
    }
  }
#endif // MOZ_WEBRTC

  // Don't save presentations for documents containing MSE content, to
  // reduce memory usage.
  if (ContainsMSEContent()) {
    return false;
  }

  if (mSubDocuments) {
    for (auto iter = mSubDocuments->Iter(); !iter.Done(); iter.Next()) {
      auto entry = static_cast<SubDocMapEntry*>(iter.Get());
      nsIDocument* subdoc = entry->mSubDocument;

      // The aIgnoreRequest we were passed is only for us, so don't pass it on.
      bool canCache = subdoc ? subdoc->CanSavePresentation(nullptr) : false;
      if (!canCache) {
        return false;
      }
    }
  }

  #ifdef MOZ_WEBSPEECH
  nsGlobalWindow* globalWindow = static_cast<nsGlobalWindow*>(win);
  if (globalWindow->HasActiveSpeechSynthesis()) {
    return false;
  }
  #endif

  return true;
}

void
nsDocument::Destroy()
{
  // The ContentViewer wants to release the document now.  So, tell our content
  // to drop any references to the document so that it can be destroyed.
  if (mIsGoingAway)
    return;

  mIsGoingAway = true;

  SetScriptGlobalObject(nullptr);
  RemovedFromDocShell();

  bool oldVal = mInUnlinkOrDeletion;
  mInUnlinkOrDeletion = true;
  uint32_t i, count = mChildren.ChildCount();
  for (i = 0; i < count; ++i) {
    mChildren.ChildAt(i)->DestroyContent();
  }
  mInUnlinkOrDeletion = oldVal;

  mLayoutHistoryState = nullptr;

  // Shut down our external resource map.  We might not need this for
  // leak-fixing if we fix nsDocumentViewer to do cycle-collection, but
  // tearing down all those frame trees right now is the right thing to do.
  mExternalResourceMap.Shutdown();

  mRegistry = nullptr;
}

void
nsDocument::RemovedFromDocShell()
{
  if (mRemovedFromDocShell)
    return;

  mRemovedFromDocShell = true;
  EnumerateActivityObservers(NotifyActivityChanged, nullptr);

  uint32_t i, count = mChildren.ChildCount();
  for (i = 0; i < count; ++i) {
    mChildren.ChildAt(i)->SaveSubtreeState();
  }
}

already_AddRefed<nsILayoutHistoryState>
nsDocument::GetLayoutHistoryState() const
{
  nsCOMPtr<nsILayoutHistoryState> state;
  if (!mScriptGlobalObject) {
    state = mLayoutHistoryState;
  } else {
    nsCOMPtr<nsIDocShell> docShell(mDocumentContainer);
    if (docShell) {
      docShell->GetLayoutHistoryState(getter_AddRefs(state));
    }
  }

  return state.forget();
}

void
nsDocument::EnsureOnloadBlocker()
{
  // If mScriptGlobalObject is null, we shouldn't be messing with the loadgroup
  // -- it's not ours.
  if (mOnloadBlockCount != 0 && mScriptGlobalObject) {
    nsCOMPtr<nsILoadGroup> loadGroup = GetDocumentLoadGroup();
    if (loadGroup) {
      // Check first to see if mOnloadBlocker is in the loadgroup.
      nsCOMPtr<nsISimpleEnumerator> requests;
      loadGroup->GetRequests(getter_AddRefs(requests));

      bool hasMore = false;
      while (NS_SUCCEEDED(requests->HasMoreElements(&hasMore)) && hasMore) {
        nsCOMPtr<nsISupports> elem;
        requests->GetNext(getter_AddRefs(elem));
        nsCOMPtr<nsIRequest> request = do_QueryInterface(elem);
        if (request && request == mOnloadBlocker) {
          return;
        }
      }

      // Not in the loadgroup, so add it.
      loadGroup->AddRequest(mOnloadBlocker, nullptr);
    }
  }
}

void
nsDocument::AsyncBlockOnload()
{
  while (mAsyncOnloadBlockCount) {
    --mAsyncOnloadBlockCount;
    BlockOnload();
  }
}

void
nsDocument::BlockOnload()
{
  if (mDisplayDocument) {
    mDisplayDocument->BlockOnload();
    return;
  }

  // If mScriptGlobalObject is null, we shouldn't be messing with the loadgroup
  // -- it's not ours.
  if (mOnloadBlockCount == 0 && mScriptGlobalObject) {
    if (!nsContentUtils::IsSafeToRunScript()) {
      // Because AddRequest may lead to OnStateChange calls in chrome,
      // block onload only when there are no script blockers.
      ++mAsyncOnloadBlockCount;
      if (mAsyncOnloadBlockCount == 1) {
        bool success = nsContentUtils::AddScriptRunner(
          NewRunnableMethod(this, &nsDocument::AsyncBlockOnload));

        // The script runner shouldn't fail to add. But if somebody broke
        // something and it does, we'll thrash at 100% cpu forever. The best
        // response is just to ignore the onload blocking request. See bug 579535.
        if (!success) {
          NS_WARNING("Disaster! Onload blocking script runner failed to add - expect bad things!");
          mAsyncOnloadBlockCount = 0;
        }
      }
      return;
    }
    nsCOMPtr<nsILoadGroup> loadGroup = GetDocumentLoadGroup();
    if (loadGroup) {
      loadGroup->AddRequest(mOnloadBlocker, nullptr);
    }
  }
  ++mOnloadBlockCount;
}

void
nsDocument::UnblockOnload(bool aFireSync)
{
  if (mDisplayDocument) {
    mDisplayDocument->UnblockOnload(aFireSync);
    return;
  }

  if (mOnloadBlockCount == 0 && mAsyncOnloadBlockCount == 0) {
    NS_NOTREACHED("More UnblockOnload() calls than BlockOnload() calls; dropping call");
    return;
  }

  --mOnloadBlockCount;

  if (mOnloadBlockCount == 0) {
    if (mScriptGlobalObject) {
      // Only manipulate the loadgroup in this case, because if mScriptGlobalObject
      // is null, it's not ours.
      if (aFireSync && mAsyncOnloadBlockCount == 0) {
        // Increment mOnloadBlockCount, since DoUnblockOnload will decrement it
        ++mOnloadBlockCount;
        DoUnblockOnload();
      } else {
        PostUnblockOnloadEvent();
      }
    } else if (mIsBeingUsedAsImage) {
      // To correctly unblock onload for a document that contains an SVG
      // image, we need to know when all of the SVG document's resources are
      // done loading, in a way comparable to |window.onload|. We fire this
      // event to indicate that the SVG should be considered fully loaded.
      // Because scripting is disabled on SVG-as-image documents, this event
      // is not accessible to content authors. (See bug 837315.)
      RefPtr<AsyncEventDispatcher> asyncDispatcher =
        new AsyncEventDispatcher(this,
                                 NS_LITERAL_STRING("MozSVGAsImageDocumentLoad"),
                                 false,
                                 false);
      asyncDispatcher->PostDOMEvent();
    }
  }
}

class nsUnblockOnloadEvent : public Runnable {
public:
  explicit nsUnblockOnloadEvent(nsDocument* aDoc) : mDoc(aDoc) {}
  NS_IMETHOD Run() {
    mDoc->DoUnblockOnload();
    return NS_OK;
  }
private:
  RefPtr<nsDocument> mDoc;
};

void
nsDocument::PostUnblockOnloadEvent()
{
  nsCOMPtr<nsIRunnable> evt = new nsUnblockOnloadEvent(this);
  nsresult rv = NS_DispatchToCurrentThread(evt);
  if (NS_SUCCEEDED(rv)) {
    // Stabilize block count so we don't post more events while this one is up
    ++mOnloadBlockCount;
  } else {
    NS_WARNING("failed to dispatch nsUnblockOnloadEvent");
  }
}

void
nsDocument::DoUnblockOnload()
{
  NS_PRECONDITION(!mDisplayDocument,
                  "Shouldn't get here for resource document");
  NS_PRECONDITION(mOnloadBlockCount != 0,
                  "Shouldn't have a count of zero here, since we stabilized in "
                  "PostUnblockOnloadEvent");

  --mOnloadBlockCount;

  if (mOnloadBlockCount != 0) {
    // We blocked again after the last unblock.  Nothing to do here.  We'll
    // post a new event when we unblock again.
    return;
  }

  if (mAsyncOnloadBlockCount != 0) {
    // We need to wait until the async onload block has been handled.
    PostUnblockOnloadEvent();
  }

  // If mScriptGlobalObject is null, we shouldn't be messing with the loadgroup
  // -- it's not ours.
  if (mScriptGlobalObject) {
    nsCOMPtr<nsILoadGroup> loadGroup = GetDocumentLoadGroup();
    if (loadGroup) {
      loadGroup->RemoveRequest(mOnloadBlocker, nullptr, NS_OK);
    }
  }
}

nsIContent*
nsDocument::GetContentInThisDocument(nsIFrame* aFrame) const
{
  for (nsIFrame* f = aFrame; f;
       f = nsLayoutUtils::GetParentOrPlaceholderForCrossDoc(f)) {
    nsIContent* content = f->GetContent();
    if (!content || content->IsInAnonymousSubtree())
      continue;

    if (content->OwnerDoc() == this) {
      return content;
    }
    // We must be in a subdocument so jump directly to the root frame.
    // GetParentOrPlaceholderForCrossDoc gets called immediately to jump up to
    // the containing document.
    f = f->PresContext()->GetPresShell()->GetRootFrame();
  }

  return nullptr;
}

void
nsDocument::DispatchPageTransition(EventTarget* aDispatchTarget,
                                   const nsAString& aType,
                                   bool aPersisted)
{
  if (!aDispatchTarget) {
    return;
  }

  PageTransitionEventInit init;
  init.mBubbles = true;
  init.mCancelable = true;
  init.mPersisted = aPersisted;

  RefPtr<PageTransitionEvent> event =
    PageTransitionEvent::Constructor(this, aType, init);

  event->SetTrusted(true);
  event->SetTarget(this);
  EventDispatcher::DispatchDOMEvent(aDispatchTarget, nullptr, event,
                                    nullptr, nullptr);
}

static bool
NotifyPageShow(nsIDocument* aDocument, void* aData)
{
  const bool* aPersistedPtr = static_cast<const bool*>(aData);
  aDocument->OnPageShow(*aPersistedPtr, nullptr);
  return true;
}

void
nsDocument::OnPageShow(bool aPersisted,
                       EventTarget* aDispatchStartTarget)
{
  mVisible = true;

  EnumerateActivityObservers(NotifyActivityChanged, nullptr);
  EnumerateExternalResources(NotifyPageShow, &aPersisted);

  Element* root = GetRootElement();
  if (aPersisted && root) {
    // Send out notifications that our <link> elements are attached.
    RefPtr<nsContentList> links = NS_GetContentList(root,
                                                      kNameSpaceID_XHTML,
                                                      NS_LITERAL_STRING("link"));

    uint32_t linkCount = links->Length(true);
    for (uint32_t i = 0; i < linkCount; ++i) {
      static_cast<HTMLLinkElement*>(links->Item(i, false))->LinkAdded();
    }
  }

  // See nsIDocument
  if (!aDispatchStartTarget) {
    // Set mIsShowing before firing events, in case those event handlers
    // move us around.
    mIsShowing = true;
  }

  if (mAnimationController) {
    mAnimationController->OnPageShow();
  }

  if (aPersisted) {
    SetImagesNeedAnimating(true);
  }

  UpdateVisibilityState();

  nsCOMPtr<EventTarget> target = aDispatchStartTarget;
  if (!target) {
    target = do_QueryInterface(GetWindow());
  }

  // Dispatch observer notification to notify observers page is shown.
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  nsIPrincipal *principal = GetPrincipal();
  os->NotifyObservers(static_cast<nsIDocument*>(this),
                      nsContentUtils::IsSystemPrincipal(principal) ?
                        "chrome-page-shown" :
                        "content-page-shown",
                      nullptr);
  if (!mObservingAppThemeChanged) {
    os->AddObserver(this, "app-theme-changed", /* ownsWeak */ false);
    mObservingAppThemeChanged = true;
  }

  DispatchPageTransition(target, NS_LITERAL_STRING("pageshow"), aPersisted);
}

static bool
NotifyPageHide(nsIDocument* aDocument, void* aData)
{
  const bool* aPersistedPtr = static_cast<const bool*>(aData);
  aDocument->OnPageHide(*aPersistedPtr, nullptr);
  return true;
}

static void
DispatchFullScreenChange(nsIDocument* aTarget)
{
  RefPtr<AsyncEventDispatcher> asyncDispatcher =
    new AsyncEventDispatcher(aTarget,
                             NS_LITERAL_STRING("mozfullscreenchange"),
                             true,
                             false);
  asyncDispatcher->PostDOMEvent();
}

void
nsDocument::OnPageHide(bool aPersisted,
                       EventTarget* aDispatchStartTarget)
{
  // Send out notifications that our <link> elements are detached,
  // but only if this is not a full unload.
  Element* root = GetRootElement();
  if (aPersisted && root) {
    RefPtr<nsContentList> links = NS_GetContentList(root,
                                                      kNameSpaceID_XHTML,
                                                      NS_LITERAL_STRING("link"));

    uint32_t linkCount = links->Length(true);
    for (uint32_t i = 0; i < linkCount; ++i) {
      static_cast<HTMLLinkElement*>(links->Item(i, false))->LinkRemoved();
    }
  }

  // See nsIDocument
  if (!aDispatchStartTarget) {
    // Set mIsShowing before firing events, in case those event handlers
    // move us around.
    mIsShowing = false;
  }

  if (mAnimationController) {
    mAnimationController->OnPageHide();
  }

  // We do not stop the animations (bug 1024343)
  // when the page is refreshing while being dragged out
  nsDocShell* docShell = mDocumentContainer.get();
  if (aPersisted && !(docShell && docShell->InFrameSwap())) {
    SetImagesNeedAnimating(false);
  }

  MozExitPointerLock();

  // Now send out a PageHide event.
  nsCOMPtr<EventTarget> target = aDispatchStartTarget;
  if (!target) {
    target = do_QueryInterface(GetWindow());
  }

  // Dispatch observer notification to notify observers page is hidden.
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    nsIPrincipal* principal = GetPrincipal();
    os->NotifyObservers(static_cast<nsIDocument*>(this),
                        nsContentUtils::IsSystemPrincipal(principal) ?
                          "chrome-page-hidden" :
                          "content-page-hidden",
                        nullptr);

    os->RemoveObserver(this, "app-theme-changed");
    mObservingAppThemeChanged = false;
  }

  DispatchPageTransition(target, NS_LITERAL_STRING("pagehide"), aPersisted);

  mVisible = false;

  UpdateVisibilityState();

  EnumerateExternalResources(NotifyPageHide, &aPersisted);
  EnumerateActivityObservers(NotifyActivityChanged, nullptr);

  if (IsFullScreenDoc()) {
    // If this document was fullscreen, we should exit fullscreen in this
    // doctree branch. This ensures that if the user navigates while in
    // fullscreen mode we don't leave its still visible ancestor documents
    // in fullscreen mode. So exit fullscreen in the document's fullscreen
    // root document, as this will exit fullscreen in all the root's
    // descendant documents. Note that documents are removed from the
    // doctree by the time OnPageHide() is called, so we must store a
    // reference to the root (in nsDocument::mFullscreenRoot) since we can't
    // just traverse the doctree to get the root.
    nsIDocument::ExitFullscreen(this, /* async */ false);

    // Since the document is removed from the doctree before OnPageHide() is
    // called, ExitFullscreen() can't traverse from the root down to *this*
    // document, so we must manually call CleanupFullscreenState() below too.
    // Note that CleanupFullscreenState() clears nsDocument::mFullscreenRoot,
    // so we *must* call it after ExitFullscreen(), not before.
    // OnPageHide() is called in every hidden (i.e. descendant) document,
    // so calling CleanupFullscreenState() here will ensure all hidden
    // documents have their fullscreen state reset.
    CleanupFullscreenState();

    // If anyone was listening to this document's state, advertizing the state
    // change would be the least of the politeness.
    DispatchFullScreenChange(this);
  }
}

void
nsDocument::WillDispatchMutationEvent(nsINode* aTarget)
{
  NS_ASSERTION(mSubtreeModifiedDepth != 0 ||
               mSubtreeModifiedTargets.Count() == 0,
               "mSubtreeModifiedTargets not cleared after dispatching?");
  ++mSubtreeModifiedDepth;
  if (aTarget) {
    // MayDispatchMutationEvent is often called just before this method,
    // so it has already appended the node to mSubtreeModifiedTargets.
    int32_t count = mSubtreeModifiedTargets.Count();
    if (!count || mSubtreeModifiedTargets[count - 1] != aTarget) {
      mSubtreeModifiedTargets.AppendObject(aTarget);
    }
  }
}

void
nsDocument::MutationEventDispatched(nsINode* aTarget)
{
  --mSubtreeModifiedDepth;
  if (mSubtreeModifiedDepth == 0) {
    int32_t count = mSubtreeModifiedTargets.Count();
    if (!count) {
      return;
    }

    nsPIDOMWindow* window = GetInnerWindow();
    if (window &&
        !window->HasMutationListeners(NS_EVENT_BITS_MUTATION_SUBTREEMODIFIED)) {
      mSubtreeModifiedTargets.Clear();
      return;
    }

    nsCOMArray<nsINode> realTargets;
    for (int32_t i = 0; i < count; ++i) {
      nsINode* possibleTarget = mSubtreeModifiedTargets[i];
      nsCOMPtr<nsIContent> content = do_QueryInterface(possibleTarget);
      if (content && content->ChromeOnlyAccess()) {
        continue;
      }

      nsINode* commonAncestor = nullptr;
      int32_t realTargetCount = realTargets.Count();
      for (int32_t j = 0; j < realTargetCount; ++j) {
        commonAncestor =
          nsContentUtils::GetCommonAncestor(possibleTarget, realTargets[j]);
        if (commonAncestor) {
          realTargets.ReplaceObjectAt(commonAncestor, j);
          break;
        }
      }
      if (!commonAncestor) {
        realTargets.AppendObject(possibleTarget);
      }
    }

    mSubtreeModifiedTargets.Clear();

    int32_t realTargetCount = realTargets.Count();
    for (int32_t k = 0; k < realTargetCount; ++k) {
      InternalMutationEvent mutation(true, eLegacySubtreeModified);
      (new AsyncEventDispatcher(realTargets[k], mutation))->
        RunDOMEventWhenSafe();
    }
  }
}

void
nsDocument::AddStyleRelevantLink(Link* aLink)
{
  NS_ASSERTION(aLink, "Passing in a null link.  Expect crashes RSN!");
#ifdef DEBUG
  nsPtrHashKey<Link>* entry = mStyledLinks.GetEntry(aLink);
  NS_ASSERTION(!entry, "Document already knows about this Link!");
  mStyledLinksCleared = false;
#endif
  (void)mStyledLinks.PutEntry(aLink);
}

void
nsDocument::ForgetLink(Link* aLink)
{
  NS_ASSERTION(aLink, "Passing in a null link.  Expect crashes RSN!");
#ifdef DEBUG
  nsPtrHashKey<Link>* entry = mStyledLinks.GetEntry(aLink);
  NS_ASSERTION(entry || mStyledLinksCleared,
               "Document knows nothing about this Link!");
#endif
  mStyledLinks.RemoveEntry(aLink);
}

void
nsDocument::DestroyElementMaps()
{
#ifdef DEBUG
  mStyledLinksCleared = true;
#endif
  mStyledLinks.Clear();
  mIdentifierMap.Clear();
  ++mExpandoAndGeneration.generation;
}

void
nsDocument::RefreshLinkHrefs()
{
  // Get a list of all links we know about.  We will reset them, which will
  // remove them from the document, so we need a copy of what is in the
  // hashtable.
  LinkArray linksToNotify(mStyledLinks.Count());
  for (auto iter = mStyledLinks.ConstIter(); !iter.Done(); iter.Next()) {
    linksToNotify.AppendElement(iter.Get()->GetKey());
  }

  // Reset all of our styled links.
  nsAutoScriptBlocker scriptBlocker;
  for (LinkArray::size_type i = 0; i < linksToNotify.Length(); i++) {
    linksToNotify[i]->ResetLinkState(true, linksToNotify[i]->ElementHasHref());
  }
}

nsresult
nsDocument::CloneDocHelper(nsDocument* clone) const
{
  clone->mIsStaticDocument = mCreatingStaticClone;

  // Init document
  nsresult rv = clone->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  if (mCreatingStaticClone) {
    nsCOMPtr<nsILoadGroup> loadGroup;

    // |mDocumentContainer| is the container of the document that is being
    // created and not the original container. See CreateStaticClone function().
    nsCOMPtr<nsIDocumentLoader> docLoader(mDocumentContainer);
    if (docLoader) {
      docLoader->GetLoadGroup(getter_AddRefs(loadGroup));
    }
    nsCOMPtr<nsIChannel> channel = GetChannel();
    nsCOMPtr<nsIURI> uri;
    if (channel) {
      NS_GetFinalChannelURI(channel, getter_AddRefs(uri));
    } else {
      uri = nsIDocument::GetDocumentURI();
    }
    clone->mChannel = channel;
    if (uri) {
      clone->ResetToURI(uri, loadGroup, NodePrincipal());
    }

    clone->SetContainer(mDocumentContainer);
  }

  // Now ensure that our clone has the same URI, base URI, and principal as us.
  // We do this after the mCreatingStaticClone block above, because that block
  // can set the base URI to an incorrect value in cases when base URI
  // information came from the channel.  So we override explicitly, and do it
  // for all these properties, in case ResetToURI messes with any of the rest of
  // them.
  clone->nsDocument::SetDocumentURI(nsIDocument::GetDocumentURI());
  clone->SetChromeXHRDocURI(mChromeXHRDocURI);
  clone->SetPrincipal(NodePrincipal());
  clone->mDocumentBaseURI = mDocumentBaseURI;
  clone->SetChromeXHRDocBaseURI(mChromeXHRDocBaseURI);

  // Set scripting object
  bool hasHadScriptObject = true;
  nsIScriptGlobalObject* scriptObject =
    GetScriptHandlingObject(hasHadScriptObject);
  NS_ENSURE_STATE(scriptObject || !hasHadScriptObject);
  if (scriptObject) {
    clone->SetScriptHandlingObject(scriptObject);
  } else {
    clone->SetScopeObject(GetScopeObject());
  }
  // Make the clone a data document
  clone->SetLoadedAsData(true);

  // Misc state

  // State from nsIDocument
  clone->mCharacterSet = mCharacterSet;
  clone->mCharacterSetSource = mCharacterSetSource;
  clone->mCompatMode = mCompatMode;
  clone->mBidiOptions = mBidiOptions;
  clone->mContentLanguage = mContentLanguage;
  clone->SetContentTypeInternal(GetContentTypeInternal());
  clone->mSecurityInfo = mSecurityInfo;

  // State from nsDocument
  clone->mType = mType;
  clone->mXMLDeclarationBits = mXMLDeclarationBits;
  clone->mBaseTarget = mBaseTarget;
  return NS_OK;
}

void
nsDocument::SetReadyStateInternal(ReadyState rs)
{
  mReadyState = rs;
  if (rs == READYSTATE_UNINITIALIZED) {
    // Transition back to uninitialized happens only to keep assertions happy
    // right before readyState transitions to something else. Make this
    // transition undetectable by Web content.
    return;
  }
  if (mTiming) {
    switch (rs) {
      case READYSTATE_LOADING:
        mTiming->NotifyDOMLoading(nsIDocument::GetDocumentURI());
        break;
      case READYSTATE_INTERACTIVE:
        mTiming->NotifyDOMInteractive(nsIDocument::GetDocumentURI());
        break;
      case READYSTATE_COMPLETE:
        mTiming->NotifyDOMComplete(nsIDocument::GetDocumentURI());
        break;
      default:
        NS_WARNING("Unexpected ReadyState value");
        break;
    }
  }
  // At the time of loading start, we don't have timing object, record time.
  if (READYSTATE_LOADING == rs) {
    mLoadingTimeStamp = mozilla::TimeStamp::Now();
  }

  RefPtr<AsyncEventDispatcher> asyncDispatcher =
    new AsyncEventDispatcher(this, NS_LITERAL_STRING("readystatechange"),
                             false, false);
  asyncDispatcher->RunDOMEventWhenSafe();
}

NS_IMETHODIMP
nsDocument::GetReadyState(nsAString& aReadyState)
{
  nsIDocument::GetReadyState(aReadyState);
  return NS_OK;
}

void
nsIDocument::GetReadyState(nsAString& aReadyState) const
{
  switch(mReadyState) {
  case READYSTATE_LOADING :
    aReadyState.AssignLiteral(MOZ_UTF16("loading"));
    break;
  case READYSTATE_INTERACTIVE :
    aReadyState.AssignLiteral(MOZ_UTF16("interactive"));
    break;
  case READYSTATE_COMPLETE :
    aReadyState.AssignLiteral(MOZ_UTF16("complete"));
    break;
  default:
    aReadyState.AssignLiteral(MOZ_UTF16("uninitialized"));
  }
}

namespace {

struct SuppressArgs
{
  nsIDocument::SuppressionType mWhat;
  uint32_t mIncrease;
};

} // namespace

static bool
SuppressEventHandlingInDocument(nsIDocument* aDocument, void* aData)
{
  SuppressArgs* args = static_cast<SuppressArgs*>(aData);
  aDocument->SuppressEventHandling(args->mWhat, args->mIncrease);
  return true;
}

void
nsDocument::SuppressEventHandling(nsIDocument::SuppressionType aWhat,
                                  uint32_t aIncrease)
{
  if (mEventsSuppressed == 0 && mAnimationsPaused == 0 &&
      aIncrease != 0 && mPresShell && mScriptGlobalObject) {
    RevokeAnimationFrameNotifications();
  }

  if (aWhat == eAnimationsOnly) {
    mAnimationsPaused += aIncrease;
  } else {
    mEventsSuppressed += aIncrease;
  }

  SuppressArgs args = { aWhat, aIncrease };
  EnumerateSubDocuments(SuppressEventHandlingInDocument, &args);
}

static void
FireOrClearDelayedEvents(nsTArray<nsCOMPtr<nsIDocument> >& aDocuments,
                         bool aFireEvents)
{
  nsIFocusManager* fm = nsFocusManager::GetFocusManager();
  if (!fm)
    return;

  for (uint32_t i = 0; i < aDocuments.Length(); ++i) {
    // NB: Don't bother trying to fire delayed events on documents that were
    // closed before this event ran.
    if (!aDocuments[i]->EventHandlingSuppressed()) {
      fm->FireDelayedEvents(aDocuments[i]);
      nsCOMPtr<nsIPresShell> shell = aDocuments[i]->GetShell();
      if (shell) {
        // Only fire events for active documents.
        bool fire = aFireEvents &&
                    aDocuments[i]->GetInnerWindow() &&
                    aDocuments[i]->GetInnerWindow()->IsCurrentInnerWindow();
        shell->FireOrClearDelayedEvents(fire);
      }
    }
  }
}

void
nsDocument::PreloadPictureOpened()
{
  mPreloadPictureDepth++;
}

void
nsDocument::PreloadPictureClosed()
{
  mPreloadPictureDepth--;
  if (mPreloadPictureDepth == 0) {
    mPreloadPictureFoundSource.SetIsVoid(true);
  } else {
    MOZ_ASSERT(mPreloadPictureDepth >= 0);
  }
}

void
nsDocument::PreloadPictureImageSource(const nsAString& aSrcsetAttr,
                                      const nsAString& aSizesAttr,
                                      const nsAString& aTypeAttr,
                                      const nsAString& aMediaAttr)
{
  // Nested pictures are not valid syntax, so while we'll eventually load them,
  // it's not worth tracking sources mixed between nesting levels to preload
  // them effectively.
  if (mPreloadPictureDepth == 1 && mPreloadPictureFoundSource.IsVoid()) {
    // <picture> selects the first matching source, so if this returns a URI we
    // needn't consider new sources until a new <picture> is encountered.
    bool found =
      HTMLImageElement::SelectSourceForTagWithAttrs(this, true, NullString(),
                                                    aSrcsetAttr, aSizesAttr,
                                                    aTypeAttr, aMediaAttr,
                                                    mPreloadPictureFoundSource);
    if (found && mPreloadPictureFoundSource.IsVoid()) {
      // Found an empty source, which counts
      mPreloadPictureFoundSource.SetIsVoid(false);
    }
  }
}

already_AddRefed<nsIURI>
nsDocument::ResolvePreloadImage(nsIURI *aBaseURI,
                                const nsAString& aSrcAttr,
                                const nsAString& aSrcsetAttr,
                                const nsAString& aSizesAttr)
{
  nsString sourceURL;
  if (mPreloadPictureDepth == 1 && !mPreloadPictureFoundSource.IsVoid()) {
    // We're in a <picture> element and found a URI from a source previous to
    // this image, use it.
    sourceURL = mPreloadPictureFoundSource;
  } else {
    // Otherwise try to use this <img> as a source
    HTMLImageElement::SelectSourceForTagWithAttrs(this, false, aSrcAttr,
                                                  aSrcsetAttr, aSizesAttr,
                                                  NullString(), NullString(),
                                                  sourceURL);
  }

  // Empty sources are not loaded by <img> (i.e. not resolved to the baseURI)
  if (sourceURL.IsEmpty()) {
    return nullptr;
  }

  // Construct into URI using passed baseURI (the parser may know of base URI
  // changes that have not reached us)
  nsresult rv;
  nsCOMPtr<nsIURI> uri;
  rv = nsContentUtils::NewURIWithDocumentCharset(getter_AddRefs(uri), sourceURL,
                                                 this, aBaseURI);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  // We don't clear mPreloadPictureFoundSource because subsequent <img> tags in
  // this this <picture> share the same <sources> (though this is not valid per
  // spec)
  return uri.forget();
}

void
nsDocument::MaybePreLoadImage(nsIURI* uri, const nsAString &aCrossOriginAttr,
                              ReferrerPolicy aReferrerPolicy)
{
  // Early exit if the img is already present in the img-cache
  // which indicates that the "real" load has already started and
  // that we shouldn't preload it.
  int16_t blockingStatus;
  if (nsContentUtils::IsImageInCache(uri, static_cast<nsIDocument *>(this)) ||
      !nsContentUtils::CanLoadImage(uri, static_cast<nsIDocument *>(this),
                                    this, NodePrincipal(), &blockingStatus,
                                    nsIContentPolicy::TYPE_INTERNAL_IMAGE_PRELOAD)) {
    return;
  }

  nsLoadFlags loadFlags = nsIRequest::LOAD_NORMAL;
  switch (Element::StringToCORSMode(aCrossOriginAttr)) {
  case CORS_NONE:
    // Nothing to do
    break;
  case CORS_ANONYMOUS:
    loadFlags |= imgILoader::LOAD_CORS_ANONYMOUS;
    break;
  case CORS_USE_CREDENTIALS:
    loadFlags |= imgILoader::LOAD_CORS_USE_CREDENTIALS;
    break;
  default:
    MOZ_CRASH("Unknown CORS mode!");
  }

  // Image not in cache - trigger preload
  RefPtr<imgRequestProxy> request;
  nsresult rv =
    nsContentUtils::LoadImage(uri,
                              static_cast<nsINode*>(this),
                              this,
                              NodePrincipal(),
                              mDocumentURI, // uri of document used as referrer
                              aReferrerPolicy,
                              nullptr,       // no observer
                              loadFlags,
                              NS_LITERAL_STRING("img"),
                              getter_AddRefs(request),
                              nsIContentPolicy::TYPE_INTERNAL_IMAGE_PRELOAD);

  // Pin image-reference to avoid evicting it from the img-cache before
  // the "real" load occurs. Unpinned in DispatchContentLoadedEvents and
  // unlink
  if (NS_SUCCEEDED(rv)) {
    mPreloadingImages.Put(uri, request.forget());
  }
}

void
nsDocument::MaybePreconnect(nsIURI* aOrigURI, mozilla::CORSMode aCORSMode)
{
  nsCOMPtr<nsIURI> uri;
  if (NS_FAILED(aOrigURI->Clone(getter_AddRefs(uri)))) {
      return;
  }

  // The URI created here is used in 2 contexts. One is nsISpeculativeConnect
  // which ignores the path and uses only the origin. The other is for the
  // document mPreloadedPreconnects de-duplication hash. Anonymous vs
  // non-Anonymous preconnects create different connections on the wire and
  // therefore should not be considred duplicates of each other and we
  // normalize the path before putting it in the hash to accomplish that.

  if (aCORSMode == CORS_ANONYMOUS) {
    uri->SetPath(NS_LITERAL_CSTRING("/anonymous"));
  } else {
    uri->SetPath(NS_LITERAL_CSTRING("/"));
  }

  if (mPreloadedPreconnects.Contains(uri)) {
    return;
  }
  mPreloadedPreconnects.Put(uri, true);

  nsCOMPtr<nsISpeculativeConnect>
    speculator(do_QueryInterface(nsContentUtils::GetIOService()));
  if (!speculator) {
    return;
  }

  if (aCORSMode == CORS_ANONYMOUS) {
    speculator->SpeculativeAnonymousConnect(uri, nullptr);
  } else {
    speculator->SpeculativeConnect(uri, nullptr);
  }
}

void
nsDocument::ForgetImagePreload(nsIURI* aURI)
{
  // Checking count is faster than hashing the URI in the common
  // case of empty table.
  if (mPreloadingImages.Count() != 0) {
    nsCOMPtr<imgIRequest> req;
    mPreloadingImages.Remove(aURI, getter_AddRefs(req));
    if (req) {
      // Make sure to cancel the request so imagelib knows it's gone.
      req->CancelAndForgetObserver(NS_BINDING_ABORTED);
    }
  }
}

EventStates
nsDocument::GetDocumentState()
{
  if (!mGotDocumentState.HasState(NS_DOCUMENT_STATE_RTL_LOCALE)) {
    if (IsDocumentRightToLeft()) {
      mDocumentState |= NS_DOCUMENT_STATE_RTL_LOCALE;
    }
    mGotDocumentState |= NS_DOCUMENT_STATE_RTL_LOCALE;
  }
  if (!mGotDocumentState.HasState(NS_DOCUMENT_STATE_WINDOW_INACTIVE)) {
    nsIPresShell* shell = GetShell();
    if (shell && shell->GetPresContext() &&
        shell->GetPresContext()->IsTopLevelWindowInactive()) {
      mDocumentState |= NS_DOCUMENT_STATE_WINDOW_INACTIVE;
    }
    mGotDocumentState |= NS_DOCUMENT_STATE_WINDOW_INACTIVE;
  }
  return mDocumentState;
}

namespace {

/**
 * Stub for LoadSheet(), since all we want is to get the sheet into
 * the CSSLoader's style cache
 */
class StubCSSLoaderObserver final : public nsICSSLoaderObserver {
  ~StubCSSLoaderObserver() {}
public:
  NS_IMETHOD
  StyleSheetLoaded(StyleSheetHandle, bool, nsresult) override
  {
    return NS_OK;
  }
  NS_DECL_ISUPPORTS
};
NS_IMPL_ISUPPORTS(StubCSSLoaderObserver, nsICSSLoaderObserver)

} // namespace

void
nsDocument::PreloadStyle(nsIURI* uri, const nsAString& charset,
                         const nsAString& aCrossOriginAttr,
                         const ReferrerPolicy aReferrerPolicy,
                         const nsAString& aIntegrity)
{
  // The CSSLoader will retain this object after we return.
  nsCOMPtr<nsICSSLoaderObserver> obs = new StubCSSLoaderObserver();

  // Charset names are always ASCII.
  CSSLoader()->LoadSheet(uri, true, NodePrincipal(),
                         NS_LossyConvertUTF16toASCII(charset),
                         obs,
                         Element::StringToCORSMode(aCrossOriginAttr),
                         aReferrerPolicy, aIntegrity);
}

nsresult
nsDocument::LoadChromeSheetSync(nsIURI* uri, bool isAgentSheet,
                                mozilla::StyleSheetHandle::RefPtr* aSheet)
{
  css::SheetParsingMode mode =
    isAgentSheet ? css::eAgentSheetFeatures
                 : css::eAuthorSheetFeatures;
  return CSSLoader()->LoadSheetSync(uri, mode, isAgentSheet, aSheet);
}

class nsDelayedEventDispatcher : public Runnable
{
public:
  explicit nsDelayedEventDispatcher(nsTArray<nsCOMPtr<nsIDocument>>& aDocuments)
  {
    mDocuments.SwapElements(aDocuments);
  }
  virtual ~nsDelayedEventDispatcher() {}

  NS_IMETHOD Run()
  {
    FireOrClearDelayedEvents(mDocuments, true);
    return NS_OK;
  }

private:
  nsTArray<nsCOMPtr<nsIDocument> > mDocuments;
};

namespace {

struct UnsuppressArgs
{
  explicit UnsuppressArgs(nsIDocument::SuppressionType aWhat)
    : mWhat(aWhat)
  {
  }

  nsIDocument::SuppressionType mWhat;
  nsTArray<nsCOMPtr<nsIDocument>> mDocs;
};

} // namespace

static bool
GetAndUnsuppressSubDocuments(nsIDocument* aDocument,
                             void* aData)
{
  UnsuppressArgs* args = static_cast<UnsuppressArgs*>(aData);
  if (args->mWhat != nsIDocument::eAnimationsOnly &&
      aDocument->EventHandlingSuppressed() > 0) {
    static_cast<nsDocument*>(aDocument)->DecreaseEventSuppression();
  } else if (args->mWhat == nsIDocument::eAnimationsOnly &&
             aDocument->AnimationsPaused()) {
    static_cast<nsDocument*>(aDocument)->ResumeAnimations();
  }

  if (args->mWhat != nsIDocument::eAnimationsOnly) {
    // No need to remember documents if we only care about animation frames.
    args->mDocs.AppendElement(aDocument);
  }

  aDocument->EnumerateSubDocuments(GetAndUnsuppressSubDocuments, aData);
  return true;
}

void
nsDocument::UnsuppressEventHandlingAndFireEvents(nsIDocument::SuppressionType aWhat,
                                                 bool aFireEvents)
{
  UnsuppressArgs args(aWhat);
  GetAndUnsuppressSubDocuments(this, &args);

  if (aWhat == nsIDocument::eAnimationsOnly) {
    // No need to fire events if we only care about animations here.
    return;
  }

  if (aFireEvents) {
    NS_DispatchToCurrentThread(new nsDelayedEventDispatcher(args.mDocs));
  } else {
    FireOrClearDelayedEvents(args.mDocs, false);
  }
}

nsISupports*
nsDocument::GetCurrentContentSink()
{
  return mParser ? mParser->GetContentSink() : nullptr;
}

nsIDocument*
nsDocument::GetTemplateContentsOwner()
{
  if (!mTemplateContentsOwner) {
    bool hasHadScriptObject = true;
    nsIScriptGlobalObject* scriptObject =
      GetScriptHandlingObject(hasHadScriptObject);

    nsCOMPtr<nsIDOMDocument> domDocument;
    nsresult rv = NS_NewDOMDocument(getter_AddRefs(domDocument),
                                    EmptyString(), // aNamespaceURI
                                    EmptyString(), // aQualifiedName
                                    nullptr, // aDoctype
                                    nsIDocument::GetDocumentURI(),
                                    nsIDocument::GetDocBaseURI(),
                                    NodePrincipal(),
                                    true, // aLoadedAsData
                                    scriptObject, // aEventObject
                                    DocumentFlavorHTML);
    NS_ENSURE_SUCCESS(rv, nullptr);

    mTemplateContentsOwner = do_QueryInterface(domDocument);
    NS_ENSURE_TRUE(mTemplateContentsOwner, nullptr);

    nsDocument* doc = static_cast<nsDocument*>(mTemplateContentsOwner.get());

    if (!scriptObject) {
      mTemplateContentsOwner->SetScopeObject(GetScopeObject());
    }

    doc->mHasHadScriptHandlingObject = hasHadScriptObject;

    // Set |doc| as the template contents owner of itself so that
    // |doc| is the template contents owner of template elements created
    // by |doc|.
    doc->mTemplateContentsOwner = doc;
  }

  return mTemplateContentsOwner;
}

void
nsDocument::SetScrollToRef(nsIURI *aDocumentURI)
{
  if (!aDocumentURI) {
    return;
  }

  nsAutoCString ref;

  // Since all URI's that pass through here aren't URL's we can't
  // rely on the nsIURI implementation for providing a way for
  // finding the 'ref' part of the URI, we'll haveto revert to
  // string routines for finding the data past '#'

  aDocumentURI->GetSpec(ref);

  nsReadingIterator<char> start, end;

  ref.BeginReading(start);
  ref.EndReading(end);

  if (FindCharInReadable('#', start, end)) {
    ++start; // Skip over the '#'

    mScrollToRef = Substring(start, end);
  }
}

void
nsDocument::ScrollToRef()
{
  if (mScrolledToRefAlready) {
    nsCOMPtr<nsIPresShell> shell = GetShell();
    if (shell) {
      shell->ScrollToAnchor();
    }
    return;
  }

  if (mScrollToRef.IsEmpty()) {
    return;
  }

  char* tmpstr = ToNewCString(mScrollToRef);
  if (!tmpstr) {
    return;
  }

  nsUnescape(tmpstr);
  nsAutoCString unescapedRef;
  unescapedRef.Assign(tmpstr);
  free(tmpstr);

  nsresult rv = NS_ERROR_FAILURE;
  // We assume that the bytes are in UTF-8, as it says in the spec:
  // http://www.w3.org/TR/html4/appendix/notes.html#h-B.2.1
  NS_ConvertUTF8toUTF16 ref(unescapedRef);

  nsCOMPtr<nsIPresShell> shell = GetShell();
  if (shell) {
    // Check an empty string which might be caused by the UTF-8 conversion
    if (!ref.IsEmpty()) {
      // Note that GoToAnchor will handle flushing layout as needed.
      rv = shell->GoToAnchor(ref, mChangeScrollPosWhenScrollingToRef);
    } else {
      rv = NS_ERROR_FAILURE;
    }

    // If UTF-8 URI failed then try to assume the string as a
    // document's charset.

    if (NS_FAILED(rv)) {
      const nsACString &docCharset = GetDocumentCharacterSet();

      rv = nsContentUtils::ConvertStringFromEncoding(docCharset,
                                                     unescapedRef,
                                                     ref);

      if (NS_SUCCEEDED(rv) && !ref.IsEmpty()) {
        rv = shell->GoToAnchor(ref, mChangeScrollPosWhenScrollingToRef);
      }
    }
    if (NS_SUCCEEDED(rv)) {
      mScrolledToRefAlready = true;
    }
  }
}

void
nsDocument::ResetScrolledToRefAlready()
{
  mScrolledToRefAlready = false;
}

void
nsDocument::SetChangeScrollPosWhenScrollingToRef(bool aValue)
{
  mChangeScrollPosWhenScrollingToRef = aValue;
}

void
nsIDocument::RegisterActivityObserver(nsISupports* aSupports)
{
  if (!mActivityObservers) {
    mActivityObservers = new nsTHashtable<nsPtrHashKey<nsISupports> >();
  }
  mActivityObservers->PutEntry(aSupports);
}

bool
nsIDocument::UnregisterActivityObserver(nsISupports* aSupports)
{
  if (!mActivityObservers) {
    return false;
  }
  nsPtrHashKey<nsISupports>* entry = mActivityObservers->GetEntry(aSupports);
  if (!entry) {
    return false;
  }
  mActivityObservers->RemoveEntry(entry);
  return true;
}

void
nsIDocument::EnumerateActivityObservers(ActivityObserverEnumerator aEnumerator,
                                        void* aData)
{
  if (!mActivityObservers)
    return;

  for (auto iter = mActivityObservers->ConstIter(); !iter.Done();
       iter.Next()) {
    aEnumerator(iter.Get()->GetKey(), aData);
  }
}

void
nsIDocument::RegisterPendingLinkUpdate(Link* aLink)
{
  MOZ_ASSERT(!mIsLinkUpdateRegistrationsForbidden);
  mLinksToUpdate.PutEntry(aLink);
  mHasLinksToUpdate = true;
}

void
nsIDocument::UnregisterPendingLinkUpdate(Link* aLink)
{
  MOZ_ASSERT(!mIsLinkUpdateRegistrationsForbidden);
  if (!mHasLinksToUpdate)
    return;

  mLinksToUpdate.RemoveEntry(aLink);
}

void
nsIDocument::FlushPendingLinkUpdates()
{
  MOZ_ASSERT(!mIsLinkUpdateRegistrationsForbidden);
  if (!mHasLinksToUpdate)
    return;

#ifdef DEBUG
  AutoRestore<bool> saved(mIsLinkUpdateRegistrationsForbidden);
  mIsLinkUpdateRegistrationsForbidden = true;
#endif
  for (auto iter = mLinksToUpdate.ConstIter(); !iter.Done(); iter.Next()) {
    Link* link = iter.Get()->GetKey();
    link->GetElement()->UpdateLinkState(link->LinkState());
  }
  mLinksToUpdate.Clear();
  mHasLinksToUpdate = false;
}

already_AddRefed<nsIDocument>
nsIDocument::CreateStaticClone(nsIDocShell* aCloneContainer)
{
  nsDocument* thisAsDoc = static_cast<nsDocument*>(this);
  mCreatingStaticClone = true;

  // Make document use different container during cloning.
  RefPtr<nsDocShell> originalShell = mDocumentContainer.get();
  SetContainer(static_cast<nsDocShell*>(aCloneContainer));
  nsCOMPtr<nsIDOMNode> clonedNode;
  nsresult rv = thisAsDoc->CloneNode(true, 1, getter_AddRefs(clonedNode));
  SetContainer(originalShell);

  RefPtr<nsDocument> clonedDoc;
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIDocument> tmp = do_QueryInterface(clonedNode);
    if (tmp) {
      clonedDoc = static_cast<nsDocument*>(tmp.get());
      if (IsStaticDocument()) {
        clonedDoc->mOriginalDocument = mOriginalDocument;
      } else {
        clonedDoc->mOriginalDocument = this;
      }

      clonedDoc->mOriginalDocument->mStaticCloneCount++;

      int32_t sheetsCount = GetNumberOfStyleSheets();
      for (int32_t i = 0; i < sheetsCount; ++i) {
        StyleSheetHandle::RefPtr sheet = GetStyleSheetAt(i);
        if (sheet) {
          if (sheet->IsApplicable()) {
            // XXXheycam Need to make ServoStyleSheet cloning work.
            if (sheet->IsGecko()) {
              RefPtr<CSSStyleSheet> clonedSheet =
                sheet->AsGecko()->Clone(nullptr, nullptr, clonedDoc, nullptr);
              NS_WARN_IF_FALSE(clonedSheet, "Cloning a stylesheet didn't work!");
              if (clonedSheet) {
                clonedDoc->AddStyleSheet(clonedSheet);
              }
            } else {
              NS_ERROR("stylo: ServoStyleSheet doesn't support cloning");
            }
          }
        }
      }

      // Iterate backwards to maintain order
      for (StyleSheetHandle sheet : Reversed(thisAsDoc->mOnDemandBuiltInUASheets)) {
        if (sheet) {
          if (sheet->IsApplicable()) {
            // XXXheycam Need to make ServoStyleSheet cloning work.
            if (sheet->IsGecko()) {
              RefPtr<CSSStyleSheet> clonedSheet =
                sheet->AsGecko()->Clone(nullptr, nullptr, clonedDoc, nullptr);
              NS_WARN_IF_FALSE(clonedSheet, "Cloning a stylesheet didn't work!");
              if (clonedSheet) {
                clonedDoc->AddOnDemandBuiltInUASheet(clonedSheet);
              }
            } else {
              NS_ERROR("stylo: ServoStyleSheet doesn't support cloning");
            }
          }
        }
      }
    }
  }
  mCreatingStaticClone = false;
  return clonedDoc.forget();
}

void
nsIDocument::UnlinkOriginalDocumentIfStatic()
{
  if (IsStaticDocument() && mOriginalDocument) {
    MOZ_ASSERT(mOriginalDocument->mStaticCloneCount > 0);
    mOriginalDocument->mStaticCloneCount--;
    mOriginalDocument = nullptr;
  }
  MOZ_ASSERT(!mOriginalDocument);
}

nsresult
nsIDocument::ScheduleFrameRequestCallback(FrameRequestCallback& aCallback,
                                          int32_t *aHandle)
{
  if (mFrameRequestCallbackCounter == INT32_MAX) {
    // Can't increment without overflowing; bail out
    return NS_ERROR_NOT_AVAILABLE;
  }
  int32_t newHandle = ++mFrameRequestCallbackCounter;

  bool alreadyRegistered = !mFrameRequestCallbacks.IsEmpty();
  DebugOnly<FrameRequest*> request =
    mFrameRequestCallbacks.AppendElement(FrameRequest(aCallback, newHandle));
  NS_ASSERTION(request, "This is supposed to be infallible!");
  if (!alreadyRegistered && mPresShell && IsEventHandlingEnabled() &&
      !AnimationsPaused()) {
    mPresShell->GetPresContext()->RefreshDriver()->
      ScheduleFrameRequestCallbacks(this);
  }

  *aHandle = newHandle;
  return NS_OK;
}

void
nsIDocument::CancelFrameRequestCallback(int32_t aHandle)
{
  // mFrameRequestCallbacks is stored sorted by handle
  if (mFrameRequestCallbacks.RemoveElementSorted(aHandle) &&
      mFrameRequestCallbacks.IsEmpty() &&
      mPresShell && IsEventHandlingEnabled()) {
    mPresShell->GetPresContext()->RefreshDriver()->
      RevokeFrameRequestCallbacks(this);
  }
}

nsresult
nsDocument::GetStateObject(nsIVariant** aState)
{
  // Get the document's current state object. This is the object backing both
  // history.state and popStateEvent.state.
  //
  // mStateObjectContainer may be null; this just means that there's no
  // current state object.

  nsCOMPtr<nsIVariant> stateObj;
  if (!mStateObjectCached && mStateObjectContainer) {
    AutoJSContext cx;
    nsIGlobalObject* sgo = GetScopeObject();
    NS_ENSURE_TRUE(sgo, NS_ERROR_UNEXPECTED);
    JS::Rooted<JSObject*> global(cx, sgo->GetGlobalJSObject());
    NS_ENSURE_TRUE(global, NS_ERROR_UNEXPECTED);
    JSAutoCompartment ac(cx, global);

    mStateObjectContainer->
      DeserializeToVariant(cx, getter_AddRefs(mStateObjectCached));
  }

  NS_IF_ADDREF(*aState = mStateObjectCached);

  return NS_OK;
}

nsDOMNavigationTiming*
nsDocument::GetNavigationTiming() const
{
  return mTiming;
}

nsresult
nsDocument::SetNavigationTiming(nsDOMNavigationTiming* aTiming)
{
  mTiming = aTiming;
  if (!mLoadingTimeStamp.IsNull() && mTiming) {
    mTiming->SetDOMLoadingTimeStamp(nsIDocument::GetDocumentURI(), mLoadingTimeStamp);
  }
  return NS_OK;
}

Element*
nsDocument::FindImageMap(const nsAString& aUseMapValue)
{
  if (aUseMapValue.IsEmpty()) {
    return nullptr;
  }

  nsAString::const_iterator start, end;
  aUseMapValue.BeginReading(start);
  aUseMapValue.EndReading(end);

  int32_t hash = aUseMapValue.FindChar('#');
  if (hash < 0) {
    return nullptr;
  }
  // aUsemap contains a '#', set start to point right after the '#'
  start.advance(hash + 1);

  if (start == end) {
    return nullptr; // aUsemap == "#"
  }

  const nsAString& mapName = Substring(start, end);

  if (!mImageMaps) {
    mImageMaps = new nsContentList(this, kNameSpaceID_XHTML, nsGkAtoms::map, nsGkAtoms::map);
  }

  uint32_t i, n = mImageMaps->Length(true);
  nsString name;
  for (i = 0; i < n; ++i) {
    nsIContent* map = mImageMaps->Item(i);
    if (map->AttrValueIs(kNameSpaceID_None, nsGkAtoms::id, mapName,
                         eCaseMatters) ||
        (map->GetAttr(kNameSpaceID_None, nsGkAtoms::name, name) &&
         mapName.Equals(name, nsCaseInsensitiveStringComparator()))) {
      return map->AsElement();
    }
  }

  return nullptr;
}

#define DEPRECATED_OPERATION(_op) #_op "Warning",
static const char* kDeprecationWarnings[] = {
#include "nsDeprecatedOperationList.h"
  nullptr
};
#undef DEPRECATED_OPERATION

#define DOCUMENT_WARNING(_op) #_op "Warning",
static const char* kDocumentWarnings[] = {
#include "nsDocumentWarningList.h"
  nullptr
};
#undef DOCUMENT_WARNING

bool
nsIDocument::HasWarnedAbout(DeprecatedOperations aOperation) const
{
  return mDeprecationWarnedAbout[aOperation];
}

void
nsIDocument::WarnOnceAbout(DeprecatedOperations aOperation,
                           bool asError /* = false */) const
{
  MOZ_ASSERT(NS_IsMainThread());
  if (HasWarnedAbout(aOperation)) {
    return;
  }
  mDeprecationWarnedAbout[aOperation] = true;
  uint32_t flags = asError ? nsIScriptError::errorFlag
                           : nsIScriptError::warningFlag;
  nsContentUtils::ReportToConsole(flags,
                                  NS_LITERAL_CSTRING("DOM Core"), this,
                                  nsContentUtils::eDOM_PROPERTIES,
                                  kDeprecationWarnings[aOperation]);
}

bool
nsIDocument::HasWarnedAbout(DocumentWarnings aWarning) const
{
  return mDocWarningWarnedAbout[aWarning];
}

void
nsIDocument::WarnOnceAbout(DocumentWarnings aWarning,
                           bool asError /* = false */,
                           const char16_t **aParams /* = nullptr */,
                           uint32_t aParamsLength /* = 0 */) const
{
  MOZ_ASSERT(NS_IsMainThread());
  if (HasWarnedAbout(aWarning)) {
    return;
  }
  mDocWarningWarnedAbout[aWarning] = true;
  uint32_t flags = asError ? nsIScriptError::errorFlag
                           : nsIScriptError::warningFlag;
  nsContentUtils::ReportToConsole(flags,
                                  NS_LITERAL_CSTRING("DOM Core"), this,
                                  nsContentUtils::eDOM_PROPERTIES,
                                  kDocumentWarnings[aWarning],
                                  aParams,
                                  aParamsLength);
}

nsresult
nsDocument::AddImage(imgIRequest* aImage)
{
  NS_ENSURE_ARG_POINTER(aImage);

  // See if the image is already in the hashtable. If it is, get the old count.
  uint32_t oldCount = 0;
  mImageTracker.Get(aImage, &oldCount);

  // Put the image in the hashtable, with the proper count.
  mImageTracker.Put(aImage, oldCount + 1);

  nsresult rv = NS_OK;

  // If this is the first insertion and we're locking images, lock this image
  // too.
  if (oldCount == 0 && mLockingImages) {
    rv = aImage->LockImage();
  }

  // If this is the first insertion and we're animating images, request
  // that this image be animated too.
  if (oldCount == 0 && mAnimatingImages) {
    nsresult rv2 = aImage->IncrementAnimationConsumers();
    rv = NS_SUCCEEDED(rv) ? rv2 : rv;
  }

  return rv;
}

nsresult
nsDocument::RemoveImage(imgIRequest* aImage, uint32_t aFlags)
{
  NS_ENSURE_ARG_POINTER(aImage);

  // Get the old count. It should exist and be > 0.
  uint32_t count = 0;
  DebugOnly<bool> found = mImageTracker.Get(aImage, &count);
  MOZ_ASSERT(found, "Removing image that wasn't in the tracker!");
  MOZ_ASSERT(count > 0, "Entry in the cache tracker with count 0!");

  // We're removing, so decrement the count.
  count--;

  // If the count is now zero, remove from the tracker.
  // Otherwise, set the new value.
  if (count != 0) {
    mImageTracker.Put(aImage, count);
    return NS_OK;
  }

  mImageTracker.Remove(aImage);

  nsresult rv = NS_OK;

  // Now that we're no longer tracking this image, unlock it if we'd
  // previously locked it.
  if (mLockingImages) {
    rv = aImage->UnlockImage();
  }

  // If we're animating images, remove our request to animate this one.
  if (mAnimatingImages) {
    nsresult rv2 = aImage->DecrementAnimationConsumers();
    rv = NS_SUCCEEDED(rv) ? rv2 : rv;
  }

  if (aFlags & REQUEST_DISCARD) {
    // Request that the image be discarded if nobody else holds a lock on it.
    // Do this even if !mLockingImages, because even if we didn't just unlock
    // this image, it might still be a candidate for discarding.
    aImage->RequestDiscard();
  }

  return rv;
}

nsresult
nsDocument::AddPlugin(nsIObjectLoadingContent* aPlugin)
{
  MOZ_ASSERT(aPlugin);
  if (!mPlugins.PutEntry(aPlugin)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  return NS_OK;
}

void
nsDocument::RemovePlugin(nsIObjectLoadingContent* aPlugin)
{
  MOZ_ASSERT(aPlugin);
  mPlugins.RemoveEntry(aPlugin);
}

static bool
AllSubDocumentPluginEnum(nsIDocument* aDocument, void* userArg)
{
  nsTArray<nsIObjectLoadingContent*>* plugins =
    reinterpret_cast< nsTArray<nsIObjectLoadingContent*>* >(userArg);
  MOZ_ASSERT(plugins);
  aDocument->GetPlugins(*plugins);
  return true;
}

void
nsDocument::GetPlugins(nsTArray<nsIObjectLoadingContent*>& aPlugins)
{
  aPlugins.SetCapacity(aPlugins.Length() + mPlugins.Count());
  for (auto iter = mPlugins.ConstIter(); !iter.Done(); iter.Next()) {
    aPlugins.AppendElement(iter.Get()->GetKey());
  }
  EnumerateSubDocuments(AllSubDocumentPluginEnum, &aPlugins);
}

nsresult
nsDocument::AddResponsiveContent(nsIContent* aContent)
{
  MOZ_ASSERT(aContent);
  MOZ_ASSERT(aContent->IsHTMLElement(nsGkAtoms::img));
  mResponsiveContent.PutEntry(aContent);
  return NS_OK;
}

void
nsDocument::RemoveResponsiveContent(nsIContent* aContent)
{
  MOZ_ASSERT(aContent);
  mResponsiveContent.RemoveEntry(aContent);
}

void
nsDocument::NotifyMediaFeatureValuesChanged()
{
  for (auto iter = mResponsiveContent.ConstIter(); !iter.Done();
       iter.Next()) {
    nsCOMPtr<nsIContent> content = iter.Get()->GetKey();
    if (content->IsHTMLElement(nsGkAtoms::img)) {
      auto* imageElement = static_cast<HTMLImageElement*>(content.get());
      imageElement->MediaFeatureValuesChanged();
    }
  }
}

nsresult
nsDocument::SetImageLockingState(bool aLocked)
{
  if (XRE_IsContentProcess() &&
      !Preferences::GetBool("image.mem.allow_locking_in_content_processes", true)) {
    return NS_OK;
  }

  // If there's no change, there's nothing to do.
  if (mLockingImages == aLocked)
    return NS_OK;

  // Otherwise, iterate over our images and perform the appropriate action.
  for (auto iter = mImageTracker.Iter(); !iter.Done(); iter.Next()) {
    imgIRequest* image = iter.Key();
    if (aLocked) {
      image->LockImage();
    } else {
      image->UnlockImage();
    }
  }

  // Update state.
  mLockingImages = aLocked;

  return NS_OK;
}

void
nsDocument::SetImagesNeedAnimating(bool aAnimating)
{
  // If there's no change, there's nothing to do.
  if (mAnimatingImages == aAnimating)
    return;

  // Otherwise, iterate over our images and perform the appropriate action.
  for (auto iter = mImageTracker.Iter(); !iter.Done(); iter.Next()) {
    imgIRequest* image = iter.Key();
    if (aAnimating) {
      image->IncrementAnimationConsumers();
    } else {
      image->DecrementAnimationConsumers();
    }
  }

  // Update state.
  mAnimatingImages = aAnimating;
}

already_AddRefed<Touch>
nsIDocument::CreateTouch(nsGlobalWindow* aView,
                         EventTarget* aTarget,
                         int32_t aIdentifier,
                         int32_t aPageX, int32_t aPageY,
                         int32_t aScreenX, int32_t aScreenY,
                         int32_t aClientX, int32_t aClientY,
                         int32_t aRadiusX, int32_t aRadiusY,
                         float aRotationAngle,
                         float aForce)
{
  MOZ_ASSERT_IF(aView, aView->IsInnerWindow());
  RefPtr<Touch> touch = new Touch(aTarget,
                                  aIdentifier,
                                  aPageX, aPageY,
                                  aScreenX, aScreenY,
                                  aClientX, aClientY,
                                  aRadiusX, aRadiusY,
                                  aRotationAngle,
                                  aForce);
  return touch.forget();
}

already_AddRefed<TouchList>
nsIDocument::CreateTouchList()
{
  RefPtr<TouchList> retval = new TouchList(ToSupports(this));
  return retval.forget();
}

already_AddRefed<TouchList>
nsIDocument::CreateTouchList(Touch& aTouch,
                             const Sequence<OwningNonNull<Touch> >& aTouches)
{
  RefPtr<TouchList> retval = new TouchList(ToSupports(this));
  retval->Append(&aTouch);
  for (uint32_t i = 0; i < aTouches.Length(); ++i) {
    retval->Append(aTouches[i].get());
  }
  return retval.forget();
}

already_AddRefed<TouchList>
nsIDocument::CreateTouchList(const Sequence<OwningNonNull<Touch> >& aTouches)
{
  RefPtr<TouchList> retval = new TouchList(ToSupports(this));
  for (uint32_t i = 0; i < aTouches.Length(); ++i) {
    retval->Append(aTouches[i].get());
  }
  return retval.forget();
}

already_AddRefed<nsDOMCaretPosition>
nsIDocument::CaretPositionFromPoint(float aX, float aY)
{
  nscoord x = nsPresContext::CSSPixelsToAppUnits(aX);
  nscoord y = nsPresContext::CSSPixelsToAppUnits(aY);
  nsPoint pt(x, y);

  FlushPendingNotifications(Flush_Layout);

  nsIPresShell *ps = GetShell();
  if (!ps) {
    return nullptr;
  }

  nsIFrame *rootFrame = ps->GetRootFrame();

  // XUL docs, unlike HTML, have no frame tree until everything's done loading
  if (!rootFrame) {
    return nullptr;
  }

  nsIFrame *ptFrame = nsLayoutUtils::GetFrameForPoint(rootFrame, pt,
      nsLayoutUtils::IGNORE_PAINT_SUPPRESSION | nsLayoutUtils::IGNORE_CROSS_DOC);
  if (!ptFrame) {
    return nullptr;
  }

  // GetContentOffsetsFromPoint requires frame-relative coordinates, so we need
  // to adjust to frame-relative coordinates before we can perform this call.
  // It should also not take into account the padding of the frame.
  nsPoint adjustedPoint = pt - ptFrame->GetOffsetTo(rootFrame);

  nsFrame::ContentOffsets offsets =
    ptFrame->GetContentOffsetsFromPoint(adjustedPoint);

  nsCOMPtr<nsIContent> node = offsets.content;
  uint32_t offset = offsets.offset;
  nsCOMPtr<nsIContent> anonNode = node;
  bool nodeIsAnonymous = node && node->IsInNativeAnonymousSubtree();
  if (nodeIsAnonymous) {
    node = ptFrame->GetContent();
    nsIContent* nonanon = node->FindFirstNonChromeOnlyAccessContent();
    nsCOMPtr<nsIDOMHTMLInputElement> input = do_QueryInterface(nonanon);
    nsCOMPtr<nsIDOMHTMLTextAreaElement> textArea = do_QueryInterface(nonanon);
    nsITextControlFrame* textFrame = do_QueryFrame(nonanon->GetPrimaryFrame());
    if (!!textFrame) {
      // If the anonymous content node has a child, then we need to make sure
      // that we get the appropriate child, as otherwise the offset may not be
      // correct when we construct a range for it.
      nsCOMPtr<nsIContent> firstChild = anonNode->GetFirstChild();
      if (firstChild) {
        anonNode = firstChild;
      }

      if (textArea) {
        offset = nsContentUtils::GetAdjustedOffsetInTextControl(ptFrame, offset);
      }

      node = nonanon;
    } else {
      node = nullptr;
      offset = 0;
    }
  }

  RefPtr<nsDOMCaretPosition> aCaretPos = new nsDOMCaretPosition(node, offset);
  if (nodeIsAnonymous) {
    aCaretPos->SetAnonymousContentNode(anonNode);
  }
  return aCaretPos.forget();
}

NS_IMETHODIMP
nsDocument::CaretPositionFromPoint(float aX, float aY, nsISupports** aCaretPos)
{
  NS_ENSURE_ARG_POINTER(aCaretPos);
  *aCaretPos = nsIDocument::CaretPositionFromPoint(aX, aY).take();
  return NS_OK;
}

static bool
IsPotentiallyScrollable(HTMLBodyElement* aBody)
{
  // An element is potentially scrollable if all of the following conditions are
  // true:

  // The element has an associated CSS layout box.
  nsIFrame* bodyFrame = aBody->GetPrimaryFrame();
  if (!bodyFrame) {
    return false;
  }

  // The element is not the HTML body element, or it is and the root element's
  // used value of the overflow-x or overflow-y properties is not visible.
  MOZ_ASSERT(aBody->GetParent() == aBody->OwnerDoc()->GetRootElement());
  nsIFrame* parentFrame = aBody->GetParent()->GetPrimaryFrame();
  if (parentFrame &&
      parentFrame->StyleDisplay()->mOverflowX == NS_STYLE_OVERFLOW_VISIBLE &&
      parentFrame->StyleDisplay()->mOverflowY == NS_STYLE_OVERFLOW_VISIBLE) {
    return false;
  }

  // The element's used value of the overflow-x or overflow-y properties is not
  // visible.
  if (bodyFrame->StyleDisplay()->mOverflowX == NS_STYLE_OVERFLOW_VISIBLE &&
      bodyFrame->StyleDisplay()->mOverflowY == NS_STYLE_OVERFLOW_VISIBLE) {
    return false;
  }

  return true;
}

Element*
nsIDocument::GetScrollingElement()
{
  if (GetCompatibilityMode() == eCompatibility_NavQuirks) {
    FlushPendingNotifications(Flush_Layout);
    HTMLBodyElement* body = GetBodyElement();
    if (body && !IsPotentiallyScrollable(body)) {
      return body;
    }

    return nullptr;
  }

  return GetRootElement();
}

void
nsIDocument::ObsoleteSheet(nsIURI *aSheetURI, ErrorResult& rv)
{
  nsresult res = CSSLoader()->ObsoleteSheet(aSheetURI);
  if (NS_FAILED(res)) {
    rv.Throw(res);
  }
}

void
nsIDocument::ObsoleteSheet(const nsAString& aSheetURI, ErrorResult& rv)
{
  nsCOMPtr<nsIURI> uri;
  nsresult res = NS_NewURI(getter_AddRefs(uri), aSheetURI);
  if (NS_FAILED(res)) {
    rv.Throw(res);
    return;
  }
  res = CSSLoader()->ObsoleteSheet(uri);
  if (NS_FAILED(res)) {
    rv.Throw(res);
  }
}

already_AddRefed<nsIURI>
nsIDocument::GetMozDocumentURIIfNotForErrorPages()
{
  if (mFailedChannel) {
    nsCOMPtr<nsIURI> failedURI;
    if (NS_SUCCEEDED(mFailedChannel->GetURI(getter_AddRefs(failedURI)))) {
      return failedURI.forget();
    }
  }

  nsCOMPtr<nsIURI> uri = GetDocumentURIObject();
  if (!uri) {
    return nullptr;
  }

  return uri.forget();
}

nsIHTMLCollection*
nsIDocument::Children()
{
  if (!mChildrenCollection) {
    mChildrenCollection = new nsContentList(this, kNameSpaceID_Wildcard,
                                            nsGkAtoms::_asterisk,
                                            nsGkAtoms::_asterisk,
                                            false);
  }

  return mChildrenCollection;
}

uint32_t
nsIDocument::ChildElementCount()
{
  return Children()->Length();
}

namespace mozilla {

// Singleton class to manage the list of fullscreen documents which are the
// root of a branch which contains fullscreen documents. We maintain this list
// so that we can easily exit all windows from fullscreen when the user
// presses the escape key.
class FullscreenRoots {
public:
  // Adds a root to the manager. Adding a root multiple times does not result
  // in duplicate entries for that item, only one.
  static void Add(nsIDocument* aRoot);

  // Iterates over every root in the root list, and calls aFunction, passing
  // each root once to aFunction. It is safe to call Add() and Remove() while
  // iterating over the list (i.e. in aFunction). Documents that are removed
  // from the manager during traversal are not traversed, and documents that
  // are added to the manager during traversal are also not traversed.
  static void ForEach(void(*aFunction)(nsIDocument* aDoc));

  // Removes a specific root from the manager.
  static void Remove(nsIDocument* aRoot);

  // Returns true if all roots added to the list have been removed.
  static bool IsEmpty();

private:

  FullscreenRoots() {
    MOZ_COUNT_CTOR(FullscreenRoots);
  }
  ~FullscreenRoots() {
    MOZ_COUNT_DTOR(FullscreenRoots);
  }

  enum {
    NotFound = uint32_t(-1)
  };
  // Looks in mRoots for aRoot. Returns the index if found, otherwise NotFound.
  static uint32_t Find(nsIDocument* aRoot);

  // Returns true if aRoot is in the list of fullscreen roots.
  static bool Contains(nsIDocument* aRoot);

  // Singleton instance of the FullscreenRoots. This is instantiated when a
  // root is added, and it is deleted when the last root is removed.
  static FullscreenRoots* sInstance;

  // List of weak pointers to roots.
  nsTArray<nsWeakPtr> mRoots;
};

FullscreenRoots* FullscreenRoots::sInstance = nullptr;

/* static */
void
FullscreenRoots::ForEach(void(*aFunction)(nsIDocument* aDoc))
{
  if (!sInstance) {
    return;
  }
  // Create a copy of the roots array, and iterate over the copy. This is so
  // that if an element is removed from mRoots we don't mess up our iteration.
  nsTArray<nsWeakPtr> roots(sInstance->mRoots);
  // Call aFunction on all entries.
  for (uint32_t i = 0; i < roots.Length(); i++) {
    nsCOMPtr<nsIDocument> root = do_QueryReferent(roots[i]);
    // Check that the root isn't in the manager. This is so that new additions
    // while we were running don't get traversed.
    if (root && FullscreenRoots::Contains(root)) {
      aFunction(root);
    }
  }
}

/* static */
bool
FullscreenRoots::Contains(nsIDocument* aRoot)
{
  return FullscreenRoots::Find(aRoot) != NotFound;
}

/* static */
void
FullscreenRoots::Add(nsIDocument* aRoot)
{
  if (!FullscreenRoots::Contains(aRoot)) {
    if (!sInstance) {
      sInstance = new FullscreenRoots();
    }
    sInstance->mRoots.AppendElement(do_GetWeakReference(aRoot));
  }
}

/* static */
uint32_t
FullscreenRoots::Find(nsIDocument* aRoot)
{
  if (!sInstance) {
    return NotFound;
  }
  nsTArray<nsWeakPtr>& roots = sInstance->mRoots;
  for (uint32_t i = 0; i < roots.Length(); i++) {
    nsCOMPtr<nsIDocument> otherRoot(do_QueryReferent(roots[i]));
    if (otherRoot == aRoot) {
      return i;
    }
  }
  return NotFound;
}

/* static */
void
FullscreenRoots::Remove(nsIDocument* aRoot)
{
  uint32_t index = Find(aRoot);
  NS_ASSERTION(index != NotFound,
    "Should only try to remove roots which are still added!");
  if (index == NotFound || !sInstance) {
    return;
  }
  sInstance->mRoots.RemoveElementAt(index);
  if (sInstance->mRoots.IsEmpty()) {
    delete sInstance;
    sInstance = nullptr;
  }
}

/* static */
bool
FullscreenRoots::IsEmpty()
{
  return !sInstance;
}

} // end namespace mozilla.
using mozilla::FullscreenRoots;

nsIDocument*
nsDocument::GetFullscreenRoot()
{
  nsCOMPtr<nsIDocument> root = do_QueryReferent(mFullscreenRoot);
  return root;
}

void
nsDocument::SetFullscreenRoot(nsIDocument* aRoot)
{
  mFullscreenRoot = do_GetWeakReference(aRoot);
}

NS_IMETHODIMP
nsDocument::MozCancelFullScreen()
{
  nsIDocument::MozCancelFullScreen();
  return NS_OK;
}

void
nsIDocument::MozCancelFullScreen()
{
  RestorePreviousFullScreenState();
}

// Runnable to set window full-screen mode. Used as a script runner
// to ensure we only call nsGlobalWindow::SetFullScreen() when it's safe to
// run script. nsGlobalWindow::SetFullScreen() dispatches a synchronous event
// (handled in chome code) which is unsafe to run if this is called in
// Element::UnbindFromTree().
class nsSetWindowFullScreen : public Runnable {
public:
  nsSetWindowFullScreen(nsIDocument* aDoc, bool aValue, gfx::VRDeviceProxy* aHMD = nullptr)
    : mDoc(aDoc), mValue(aValue), mHMD(aHMD) {}

  NS_IMETHOD Run()
  {
    if (mDoc->GetWindow()) {
      mDoc->GetWindow()->SetFullScreenInternal(mValue, false, mHMD);
    }
    return NS_OK;
  }

private:
  nsCOMPtr<nsIDocument> mDoc;
  bool mValue;
  RefPtr<gfx::VRDeviceProxy> mHMD;
};

static nsIDocument*
GetFullscreenRootDocument(nsIDocument* aDoc)
{
  if (!aDoc) {
    return nullptr;
  }
  nsIDocument* doc = aDoc;
  nsIDocument* parent;
  while ((parent = doc->GetParentDocument()) &&
         (!nsContentUtils::IsFullscreenApiContentOnly() ||
          !nsContentUtils::IsChromeDoc(parent))) {
    doc = parent;
  }
  return doc;
}

static void
SetWindowFullScreen(nsIDocument* aDoc, bool aValue, gfx::VRDeviceProxy *aVRHMD = nullptr)
{
  // Maintain list of fullscreen root documents.
  nsCOMPtr<nsIDocument> root = GetFullscreenRootDocument(aDoc);
  if (aValue) {
    FullscreenRoots::Add(root);
  } else {
    FullscreenRoots::Remove(root);
  }
  if (!nsContentUtils::IsFullscreenApiContentOnly()) {
    nsContentUtils::AddScriptRunner(new nsSetWindowFullScreen(aDoc, aValue, aVRHMD));
  }
}

class nsCallExitFullscreen : public Runnable {
public:
  explicit nsCallExitFullscreen(nsIDocument* aDoc)
    : mDoc(aDoc) {}
  NS_IMETHOD Run()
  {
    nsDocument::ExitFullscreen(mDoc);
    return NS_OK;
  }
private:
  nsCOMPtr<nsIDocument> mDoc;
};

/* static */
void
nsIDocument::ExitFullscreen(nsIDocument* aDoc, bool aRunAsync)
{
  if (aDoc && !aDoc->IsFullScreenDoc()) {
    return;
  }
  if (aRunAsync) {
    NS_DispatchToCurrentThread(new nsCallExitFullscreen(aDoc));
    return;
  }
  nsDocument::ExitFullscreen(aDoc);
}

// Returns true if the document is a direct child of a cross process parent
// mozbrowser iframe or TabParent. This is the case when the document has
// a null parent and its DocShell reports that it is a browser frame, or
// we can get a TabChild from it.
static bool
HasCrossProcessParent(nsIDocument* aDocument)
{
  if (XRE_GetProcessType() != GeckoProcessType_Content) {
    return false;
  }
  if (aDocument->GetParentDocument() != nullptr) {
    return false;
  }
  nsPIDOMWindow* win = aDocument->GetWindow();
  if (!win) {
    return false;
  }
  nsCOMPtr<nsIDocShell> docShell = win->GetDocShell();
  if (!docShell) {
    return false;
  }
  TabChild* tabChild(TabChild::GetFrom(docShell));
  if (!tabChild) {
    return false;
  }

  return true;
}

static bool
CountFullscreenSubDocuments(nsIDocument* aDoc, void* aData)
{
  if (aDoc->IsFullScreenDoc()) {
    uint32_t* count = static_cast<uint32_t*>(aData);
    (*count)++;
  }
  return true;
}

static uint32_t
CountFullscreenSubDocuments(nsIDocument* aDoc)
{
  uint32_t count = 0;
  aDoc->EnumerateSubDocuments(CountFullscreenSubDocuments, &count);
  return count;
}

bool
nsDocument::IsFullscreenLeaf()
{
  // A fullscreen leaf document is fullscreen, and has no fullscreen
  // subdocuments.
  if (!IsFullScreenDoc()) {
    return false;
  }
  return CountFullscreenSubDocuments(this) == 0;
}

static bool
ResetFullScreen(nsIDocument* aDocument, void* aData)
{
  if (aDocument->IsFullScreenDoc()) {
    NS_ASSERTION(CountFullscreenSubDocuments(aDocument) <= 1,
        "Should have at most 1 fullscreen subdocument.");
    static_cast<nsDocument*>(aDocument)->CleanupFullscreenState();
    NS_ASSERTION(!aDocument->IsFullScreenDoc(), "Should reset full-screen");
    nsTArray<nsIDocument*>* changed = reinterpret_cast<nsTArray<nsIDocument*>*>(aData);
    changed->AppendElement(aDocument);

    if (HasCrossProcessParent(aDocument)) {
      // We're at the top of the content-process side doc tree. Ask the parent
      // process to exit fullscreen.
      nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
      os->NotifyObservers(aDocument, "ask-parent-to-exit-fullscreen", nullptr);
    }

    // Dispatch a notification so that if this document has any
    // cross-process subdocuments, they'll be notified to exit fullscreen.
    // The BrowserElementParent listens for this event and performs the
    // cross process notification if it has a remote child process.
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    os->NotifyObservers(aDocument, "ask-children-to-exit-fullscreen", nullptr);

    aDocument->EnumerateSubDocuments(ResetFullScreen, aData);
  }
  return true;
}

static void
ExitFullscreenInDocTree(nsIDocument* aMaybeNotARootDoc)
{
  MOZ_ASSERT(aMaybeNotARootDoc);
  nsCOMPtr<nsIDocument> root = aMaybeNotARootDoc->GetFullscreenRoot();
  NS_ASSERTION(root, "Should have root when in fullscreen!");
  if (!root) {
    return;
  }
  if (!root->IsFullScreenDoc()) {
    // If a document was detached before exiting from fullscreen, it is
    // possible that the root had left fullscreen state. In this case,
    // we would not get anything from the ResetFullScreen() call. Root's
    // not being a fullscreen doc also means the widget should have
    // exited fullscreen state. It means even if we do not return here,
    // we would actually do nothing below except crashing ourselves via
    // dispatching the "MozDOMFullscreen:Exited" event to an nonexistent
    // document.
    return;
  }

  // Stores a list of documents to which we must dispatch "mozfullscreenchange".
  // We're required by the spec to dispatch the events in leaf-to-root
  // order when exiting fullscreen, but we traverse the doctree in a
  // root-to-leaf order, so we save references to the documents we must
  // dispatch to so that we dispatch in the specified order.
  AutoTArray<nsIDocument*, 8> changed;

  // Walk the tree of fullscreen documents, and reset their fullscreen state.
  ResetFullScreen(root, static_cast<void*>(&changed));

  // Dispatch "mozfullscreenchange" events. Note this loop is in reverse
  // order so that the events for the leaf document arrives before the root
  // document, as required by the spec.
  for (uint32_t i = 0; i < changed.Length(); ++i) {
    DispatchFullScreenChange(changed[changed.Length() - i - 1]);
  }

  NS_ASSERTION(!root->IsFullScreenDoc(),
    "Fullscreen root should no longer be a fullscreen doc...");

  // Dispatch MozExitedDomFullscreen to the last document in
  // the list since we want this event to follow the same path
  // MozEnteredDomFullscreen dispatched.
  RefPtr<AsyncEventDispatcher> asyncDispatcher = new AsyncEventDispatcher(
    changed.LastElement(), NS_LITERAL_STRING("MozExitedDomFullscreen"), true, true);
  asyncDispatcher->PostDOMEvent();
  // Move the top-level window out of fullscreen mode.
  SetWindowFullScreen(root, false);
}

/* static */
void
nsDocument::ExitFullscreen(nsIDocument* aDoc)
{
  // Unlock the pointer, if it's locked.
  nsCOMPtr<Element> pointerLockedElement =
    do_QueryReferent(EventStateManager::sPointerLockedElement);
  if (pointerLockedElement) {
    UnlockPointer();
  }

  if (aDoc)  {
    ExitFullscreenInDocTree(aDoc);
    return;
  }

  // Clear fullscreen stacks in all fullscreen roots' descendant documents.
  FullscreenRoots::ForEach(&ExitFullscreenInDocTree);
  NS_ASSERTION(FullscreenRoots::IsEmpty(),
      "Should have exited all fullscreen roots from fullscreen");
}

bool
GetFullscreenLeaf(nsIDocument* aDoc, void* aData)
{
  if (aDoc->IsFullscreenLeaf()) {
    nsIDocument** result = static_cast<nsIDocument**>(aData);
    *result = aDoc;
    return false;
  } else if (aDoc->IsFullScreenDoc()) {
    aDoc->EnumerateSubDocuments(GetFullscreenLeaf, aData);
  }
  return true;
}

static nsIDocument*
GetFullscreenLeaf(nsIDocument* aDoc)
{
  nsIDocument* leaf = nullptr;
  GetFullscreenLeaf(aDoc, &leaf);
  if (leaf) {
    return leaf;
  }
  // Otherwise we could be either in a non-fullscreen doc tree, or we're
  // below the fullscreen doc. Start the search from the root.
  nsIDocument* root = GetFullscreenRootDocument(aDoc);
  // Check that the root is actually fullscreen so we don't waste time walking
  // around its descendants.
  if (!root->IsFullScreenDoc()) {
    return nullptr;
  }
  GetFullscreenLeaf(root, &leaf);
  return leaf;
}

void
nsDocument::RestorePreviousFullScreenState()
{
  NS_ASSERTION(!IsFullScreenDoc() || !FullscreenRoots::IsEmpty(),
    "Should have at least 1 fullscreen root when fullscreen!");
  NS_ASSERTION(!nsContentUtils::IsFullscreenApiContentOnly() ||
               !nsContentUtils::IsChromeDoc(this),
               "Should not run RestorePreviousFullScreenState() on "
               "chrome document when fullscreen is content only");

  if (!IsFullScreenDoc() || !GetWindow() || FullscreenRoots::IsEmpty()) {
    return;
  }

  // If fullscreen mode is updated the pointer should be unlocked
  nsCOMPtr<Element> pointerLockedElement =
    do_QueryReferent(EventStateManager::sPointerLockedElement);
  if (pointerLockedElement) {
    UnlockPointer();
  }

  nsCOMPtr<nsIDocument> fullScreenDoc = GetFullscreenLeaf(this);

  // The fullscreen document may contain a <iframe mozbrowser> element which
  // has a cross process child. So send a notification so that its browser
  // parent will send a message to its child process to also exit fullscreen.
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  os->NotifyObservers(fullScreenDoc, "ask-children-to-exit-fullscreen", nullptr);

  // Clear full-screen stacks in all descendant in process documents, bottom up.
  nsIDocument* doc = fullScreenDoc;
  while (doc != this) {
    NS_ASSERTION(doc->IsFullScreenDoc(), "Should be full-screen doc");
    static_cast<nsDocument*>(doc)->CleanupFullscreenState();
    UnlockPointer();
    DispatchFullScreenChange(doc);
    doc = doc->GetParentDocument();
  }

  // Roll-back full-screen state to previous full-screen element.
  NS_ASSERTION(doc == this, "Must have reached this doc.");
  while (doc != nullptr) {
    static_cast<nsDocument*>(doc)->FullScreenStackPop();
    UnlockPointer();
    DispatchFullScreenChange(doc);
    if (static_cast<nsDocument*>(doc)->mFullScreenStack.IsEmpty()) {
      if (HasCrossProcessParent(doc)) {
        // Send notification to the parent process to tell it to rollback to
        // the previous fullscreen elements in its fullscreen element stacks.
        nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
        os->NotifyObservers(doc, "ask-parent-to-rollback-fullscreen", nullptr);
      }
      // Full-screen stack in document is empty. Go back up to the parent
      // document. We'll pop the containing element off its stack, and use
      // its next full-screen element as the full-screen element.
      static_cast<nsDocument*>(doc)->CleanupFullscreenState();
      doc = doc->GetParentDocument();
    } else {
      // Else we popped the top of the stack, and there's still another
      // element in there, so that will become the full-screen element.
      if (fullScreenDoc != doc) {
        // We've popped so enough off the stack that we've rolled back to
        // a fullscreen element in a parent document. If this document isn't
        // approved for fullscreen, or if it's cross origin, dispatch an
        // event to chrome so it knows to show the authorization/warning UI.
        if (!nsContentUtils::HaveEqualPrincipals(fullScreenDoc, doc) ||
            (!nsContentUtils::IsSitePermAllow(doc->NodePrincipal(), "fullscreen") &&
             !static_cast<nsDocument*>(doc)->mIsApprovedForFullscreen)) {
          RefPtr<AsyncEventDispatcher> asyncDispatcher =
            new AsyncEventDispatcher(doc,
                  NS_LITERAL_STRING("MozEnteredDomFullscreen"),
                  true,
                  true);
          asyncDispatcher->PostDOMEvent();
        }
      }

      if (!nsContentUtils::HaveEqualPrincipals(doc, fullScreenDoc)) {
        // The origin which is fullscreen changed. Send a notification to
        // the root process so that a warning or approval UI can be shown
        // as necessary.
        nsAutoString origin;
        nsContentUtils::GetUTFOrigin(doc->NodePrincipal(), origin);
        nsIDocument* root = GetFullscreenRootDocument(doc);
        nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
        os->NotifyObservers(root, "fullscreen-origin-change", origin.get());
      }

      break;
    }
  }

  if (doc == nullptr) {
    // We moved all documents in this doctree out of fullscreen mode,
    // move the top-level window out of fullscreen mode.
    NS_ASSERTION(!GetFullscreenRootDocument(this)->IsFullScreenDoc(),
      "Should have cleared all docs' stacks");
    RefPtr<AsyncEventDispatcher> asyncDispatcher = new AsyncEventDispatcher(
      this, NS_LITERAL_STRING("MozExitedDomFullscreen"), true, true);
    asyncDispatcher->PostDOMEvent();
    SetWindowFullScreen(this, false);
  }
}

bool
nsDocument::IsFullScreenDoc()
{
  return GetFullScreenElement() != nullptr;
}

FullScreenOptions::FullScreenOptions()
{
}

class nsCallRequestFullScreen : public Runnable
{
public:
  explicit nsCallRequestFullScreen(Element* aElement, FullScreenOptions& aOptions)
    : mElement(aElement),
      mDoc(aElement->OwnerDoc()),
      mWasCallerChrome(nsContentUtils::IsCallerChrome()),
      mHadRequestPending(static_cast<nsDocument*>(mDoc.get())->
                         mAsyncFullscreenPending),
      mOptions(aOptions)
  {
    static_cast<nsDocument*>(mDoc.get())->
      mAsyncFullscreenPending = true;
  }

  NS_IMETHOD Run()
  {
    static_cast<nsDocument*>(mDoc.get())->
      mAsyncFullscreenPending = mHadRequestPending;
    nsDocument* doc = static_cast<nsDocument*>(mDoc.get());
    doc->RequestFullScreen(mElement,
                           mOptions,
                           mWasCallerChrome,
                           /* aNotifyOnOriginChange */ true);
    return NS_OK;
  }

  RefPtr<Element> mElement;
  nsCOMPtr<nsIDocument> mDoc;
  bool mWasCallerChrome;
  bool mHadRequestPending;
  FullScreenOptions mOptions;
};

void
nsDocument::AsyncRequestFullScreen(Element* aElement,
                                   FullScreenOptions& aOptions)
{
  NS_ASSERTION(aElement,
    "Must pass non-null element to nsDocument::AsyncRequestFullScreen");
  if (!aElement) {
    return;
  }
  // Request full-screen asynchronously.
  nsCOMPtr<nsIRunnable> event(new nsCallRequestFullScreen(aElement, aOptions));
  NS_DispatchToCurrentThread(event);
}

static void
LogFullScreenDenied(bool aLogFailure, const char* aMessage, nsIDocument* aDoc)
{
  if (!aLogFailure) {
    return;
  }
  RefPtr<AsyncEventDispatcher> asyncDispatcher =
    new AsyncEventDispatcher(aDoc,
                             NS_LITERAL_STRING("mozfullscreenerror"),
                             true,
                             false);
  asyncDispatcher->PostDOMEvent();
  nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                  NS_LITERAL_CSTRING("DOM"), aDoc,
                                  nsContentUtils::eDOM_PROPERTIES,
                                  aMessage);
}

nsresult
nsDocument::AddFullscreenApprovedObserver()
{
  if (mHasFullscreenApprovedObserver ||
      !Preferences::GetBool("full-screen-api.approval-required")) {
    return NS_OK;
  }

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  NS_ENSURE_TRUE(os, NS_ERROR_FAILURE);

  nsresult res = os->AddObserver(this, "fullscreen-approved", true);
  NS_ENSURE_SUCCESS(res, res);

  mHasFullscreenApprovedObserver = true;

  return NS_OK;
}

nsresult
nsDocument::RemoveFullscreenApprovedObserver()
{
  if (!mHasFullscreenApprovedObserver) {
    return NS_OK;
  }
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  NS_ENSURE_TRUE(os, NS_ERROR_FAILURE);

  nsresult res = os->RemoveObserver(this, "fullscreen-approved");
  NS_ENSURE_SUCCESS(res, res);

  mHasFullscreenApprovedObserver = false;

  return NS_OK;
}

static void
UpdateViewportScrollbarOverrideForFullscreen(nsIDocument* aDoc)
{
  if (nsIPresShell* presShell = aDoc->GetShell()) {
    if (nsPresContext* presContext = presShell->GetPresContext()) {
      presContext->UpdateViewportScrollbarStylesOverride();
    }
  }
}

static void
ClearFullscreenStateOnElement(Element* aElement)
{
  // Remove any VR state properties
  aElement->DeleteProperty(nsGkAtoms::vr_state);
  // Remove styles from existing top element.
  EventStateManager::SetFullScreenState(aElement, false);
  // Reset iframe fullscreen flag.
  if (aElement->IsHTMLElement(nsGkAtoms::iframe)) {
    static_cast<HTMLIFrameElement*>(aElement)->SetFullscreenFlag(false);
  }
}

void
nsDocument::CleanupFullscreenState()
{
  // Iterate the fullscreen stack and clear the fullscreen states.
  // Since we also need to clear the fullscreen-ancestor state, and
  // currently fullscreen elements can only be placed in hierarchy
  // order in the stack, reversely iterating the stack could be more
  // efficient. NOTE that fullscreen-ancestor state would be removed
  // in bug 1199529, and the elements may not in hierarchy order
  // after bug 1195213.
  for (nsWeakPtr& weakPtr : Reversed(mFullScreenStack)) {
    if (nsCOMPtr<Element> element = do_QueryReferent(weakPtr)) {
      ClearFullscreenStateOnElement(element);
    }
  }
  SetApprovedForFullscreen(false);
  RemoveFullscreenApprovedObserver();
  mFullScreenStack.Clear();
  mFullscreenRoot = nullptr;
  UpdateViewportScrollbarOverrideForFullscreen(this);
}

bool
nsDocument::FullScreenStackPush(Element* aElement)
{
  NS_ASSERTION(aElement, "Must pass non-null to FullScreenStackPush()");
  Element* top = FullScreenStackTop();
  if (top == aElement || !aElement) {
    return false;
  }
  EventStateManager::SetFullScreenState(aElement, true);
  mFullScreenStack.AppendElement(do_GetWeakReference(aElement));
  NS_ASSERTION(GetFullScreenElement() == aElement, "Should match");
  UpdateViewportScrollbarOverrideForFullscreen(this);
  return true;
}

void
nsDocument::FullScreenStackPop()
{
  if (mFullScreenStack.IsEmpty()) {
    return;
  }

  ClearFullscreenStateOnElement(FullScreenStackTop());

  // Remove top element. Note the remaining top element in the stack
  // will not have full-screen style bits set, so we will need to restore
  // them on the new top element before returning.
  uint32_t last = mFullScreenStack.Length() - 1;
  mFullScreenStack.RemoveElementAt(last);

  // Pop from the stack null elements (references to elements which have
  // been GC'd since they were added to the stack) and elements which are
  // no longer in this document.
  while (!mFullScreenStack.IsEmpty()) {
    Element* element = FullScreenStackTop();
    if (!element || !element->IsInUncomposedDoc() || element->OwnerDoc() != this) {
      NS_ASSERTION(!element->IsFullScreenAncestor(),
                   "Should have already removed full-screen styles");
      uint32_t last = mFullScreenStack.Length() - 1;
      mFullScreenStack.RemoveElementAt(last);
    } else {
      // The top element of the stack is now an in-doc element. Return here.
      break;
    }
  }

  UpdateViewportScrollbarOverrideForFullscreen(this);
}

Element*
nsDocument::FullScreenStackTop()
{
  if (mFullScreenStack.IsEmpty()) {
    return nullptr;
  }
  uint32_t last = mFullScreenStack.Length() - 1;
  nsCOMPtr<Element> element(do_QueryReferent(mFullScreenStack[last]));
  NS_ASSERTION(element, "Should have full-screen element!");
  NS_ASSERTION(element->IsInUncomposedDoc(), "Full-screen element should be in doc");
  NS_ASSERTION(element->OwnerDoc() == this, "Full-screen element should be in this doc");
  return element;
}

/* virtual */ nsTArray<Element*>
nsDocument::GetFullscreenStack() const
{
  nsTArray<Element*> elements;
  for (const nsWeakPtr& ptr : mFullScreenStack) {
    if (nsCOMPtr<Element> elem = do_QueryReferent(ptr)) {
      MOZ_ASSERT(elem->State().HasState(NS_EVENT_STATE_FULL_SCREEN));
      elements.AppendElement(elem);
    }
  }
  return elements;
}

// Returns true if aDoc is in the focused tab in the active window.
static bool
IsInActiveTab(nsIDocument* aDoc)
{
  nsCOMPtr<nsIDocShell> docshell = aDoc->GetDocShell();
  if (!docshell) {
    return false;
  }

  bool isActive = false;
  docshell->GetIsActive(&isActive);
  if (!isActive) {
    return false;
  }

  nsCOMPtr<nsIDocShellTreeItem> rootItem;
  docshell->GetRootTreeItem(getter_AddRefs(rootItem));
  if (!rootItem) {
    return false;
  }
  nsCOMPtr<nsIDOMWindow> rootWin = rootItem->GetWindow();
  if (!rootWin) {
    return false;
  }

  nsIFocusManager* fm = nsFocusManager::GetFocusManager();
  if (!fm) {
    return false;
  }

  nsCOMPtr<nsIDOMWindow> activeWindow;
  fm->GetActiveWindow(getter_AddRefs(activeWindow));
  if (!activeWindow) {
    return false;
  }

  return activeWindow == rootWin;
}

nsresult nsDocument::RemoteFrameFullscreenChanged(nsIDOMElement* aFrameElement,
                                                  const nsAString& aOrigin)
{
  // Ensure the frame element is the fullscreen element in this document.
  // If the frame element is already the fullscreen element in this document,
  // this has no effect.
  nsCOMPtr<nsIContent> content(do_QueryInterface(aFrameElement));
  FullScreenOptions opts;
  RequestFullScreen(content->AsElement(),
                    opts,
                    /* aWasCallerChrome */ false,
                    /* aNotifyOnOriginChange */ false);

  // Origin changed in child process, send notifiction, so that chrome can
  // update the UI to reflect the fullscreen origin change if necessary.
  // The BrowserElementChild listens on this, and forwards it over its
  // parent process, where it is redispatched. Chrome (in the root process,
  // which could be *this* process) listens for this notification so that
  // it can show a warning or approval UI.
  if (!aOrigin.IsEmpty()) {
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    os->NotifyObservers(GetFullscreenRootDocument(this),
                        "fullscreen-origin-change",
                        PromiseFlatString(aOrigin).get());
  }

  return NS_OK;
}

nsresult nsDocument::RemoteFrameFullscreenReverted()
{
  RestorePreviousFullScreenState();
  return NS_OK;
}

static void
ReleaseVRDeviceProxyRef(void *, nsIAtom*, void *aPropertyValue, void *)
{
  if (aPropertyValue) {
    static_cast<gfx::VRDeviceProxy*>(aPropertyValue)->Release();
  }
}

void
nsDocument::RequestFullScreen(Element* aElement,
                              FullScreenOptions& aOptions,
                              bool aWasCallerChrome,
                              bool aNotifyOnOriginChange)
{
  NS_ASSERTION(aElement,
    "Must pass non-null element to nsDocument::RequestFullScreen");
  if (!aElement || aElement == GetFullScreenElement()) {
    return;
  }
  if (!aElement->IsInUncomposedDoc()) {
    LogFullScreenDenied(true, "FullScreenDeniedNotInDocument", this);
    return;
  }
  if (aElement->OwnerDoc() != this) {
    LogFullScreenDenied(true, "FullScreenDeniedMovedDocument", this);
    return;
  }
  if (!GetWindow()) {
    LogFullScreenDenied(true, "FullScreenDeniedLostWindow", this);
    return;
  }
  if (nsContentUtils::IsFullscreenApiContentOnly() &&
      nsContentUtils::IsChromeDoc(this)) {
    // Block fullscreen requests in the chrome document when the fullscreen API
    // is configured for content only.
    LogFullScreenDenied(true, "FullScreenDeniedContentOnly", this);
    return;
  }
  if (!IsFullScreenEnabled(aWasCallerChrome, true)) {
    // IsFullScreenEnabled calls LogFullScreenDenied, no need to log.
    return;
  }
  if (GetFullScreenElement() &&
      !nsContentUtils::ContentIsDescendantOf(aElement, GetFullScreenElement())) {
    // If this document is full-screen, only grant full-screen requests from
    // a descendant of the current full-screen element.
    LogFullScreenDenied(true, "FullScreenDeniedNotDescendant", this);
    return;
  }
  if (!nsContentUtils::IsChromeDoc(this) && !IsInActiveTab(this)) {
    LogFullScreenDenied(true, "FullScreenDeniedNotFocusedTab", this);
    return;
  }
  // Deny requests when a windowed plugin is focused.
  nsIFocusManager* fm = nsFocusManager::GetFocusManager();
  if (!fm) {
    NS_WARNING("Failed to retrieve focus manager in full-screen request.");
    return;
  }
  nsCOMPtr<nsIDOMElement> focusedElement;
  fm->GetFocusedElement(getter_AddRefs(focusedElement));
  if (focusedElement) {
    nsCOMPtr<nsIContent> content = do_QueryInterface(focusedElement);
    if (nsContentUtils::HasPluginWithUncontrolledEventDispatch(content)) {
      LogFullScreenDenied(true, "FullScreenDeniedFocusedPlugin", this);
      return;
    }
  }

  // Stash a reference to any existing fullscreen doc, we'll use this later
  // to detect if the origin which is fullscreen has changed.
  nsCOMPtr<nsIDocument> previousFullscreenDoc = GetFullscreenLeaf(this);

  AddFullscreenApprovedObserver();

  // Stores a list of documents which we must dispatch "mozfullscreenchange"
  // too. We're required by the spec to dispatch the events in root-to-leaf
  // order, but we traverse the doctree in a leaf-to-root order, so we save
  // references to the documents we must dispatch to so that we get the order
  // as specified.
  AutoTArray<nsIDocument*, 8> changed;

  // Remember the root document, so that if a full-screen document is hidden
  // we can reset full-screen state in the remaining visible full-screen documents.
  nsIDocument* fullScreenRootDoc = GetFullscreenRootDocument(this);
  if (fullScreenRootDoc->IsFullScreenDoc()) {
    // A document is already in fullscreen, unlock the mouse pointer
    // before setting a new document to fullscreen
    UnlockPointer();
  }

  // If a document is already in fullscreen, then unlock the mouse pointer
  // before setting a new document to fullscreen
  nsCOMPtr<Element> pointerLockedElement =
    do_QueryReferent(EventStateManager::sPointerLockedElement);
  if (pointerLockedElement) {
    UnlockPointer();
  }

  // Process options -- in this case, just HMD
  if (aOptions.mVRHMDDevice) {
    RefPtr<gfx::VRDeviceProxy> hmdRef = aOptions.mVRHMDDevice;
    aElement->SetProperty(nsGkAtoms::vr_state, hmdRef.forget().take(),
                          ReleaseVRDeviceProxyRef,
                          true);
  }

  // Set the full-screen element. This sets the full-screen style on the
  // element, and the full-screen-ancestor styles on ancestors of the element
  // in this document.
  DebugOnly<bool> x = FullScreenStackPush(aElement);
  NS_ASSERTION(x, "Full-screen state of requesting doc should always change!");
  changed.AppendElement(this);

  // Propagate up the document hierarchy, setting the full-screen element as
  // the element's container in ancestor documents. This also sets the
  // appropriate css styles as well. Note we don't propagate down the
  // document hierarchy, the full-screen element (or its container) is not
  // visible there. Stop when we reach the root document.
  nsIDocument* child = this;
  while (true) {
    child->SetFullscreenRoot(fullScreenRootDoc);
    NS_ASSERTION(child->GetFullscreenRoot() == fullScreenRootDoc,
        "Fullscreen root should be set!");
    if (child == fullScreenRootDoc) {
      break;
    }
    nsIDocument* parent = child->GetParentDocument();
    Element* element = parent->FindContentForSubDocument(child)->AsElement();
    if (static_cast<nsDocument*>(parent)->FullScreenStackPush(element)) {
      changed.AppendElement(parent);
      child = parent;
    } else {
      // We've reached either the root, or a point in the doctree where the
      // new full-screen element container is the same as the previous
      // full-screen element's container. No more changes need to be made
      // to the full-screen stacks of documents further up the tree.
      break;
    }
  }

  // Dispatch "mozfullscreenchange" events. Note this loop is in reverse
  // order so that the events for the root document arrives before the leaf
  // document, as required by the spec.
  for (uint32_t i = 0; i < changed.Length(); ++i) {
    DispatchFullScreenChange(changed[changed.Length() - i - 1]);
  }

  // If this document hasn't already been approved in this session,
  // check to see if the user has granted the fullscreen access
  // to the document's principal's host, if it has one. Note that documents
  // in web apps which are the same origin as the web app are considered
  // trusted and so are automatically approved.
  if (!mIsApprovedForFullscreen) {
    mIsApprovedForFullscreen =
      !Preferences::GetBool("full-screen-api.approval-required") ||
      NodePrincipal()->GetAppStatus() >= nsIPrincipal::APP_STATUS_INSTALLED ||
      nsContentUtils::IsSitePermAllow(NodePrincipal(), "fullscreen");
  }

  // If this document, or a document with the same principal has not
  // already been approved for fullscreen this fullscreen-session, dispatch
  // an event so that chrome knows to pop up a warning/approval UI.
  // Note previousFullscreenDoc=nullptr upon first entry, so we always
  // take this path on the first time we enter fullscreen in a fullscreen
  // session.
  if (!mIsApprovedForFullscreen ||
      !nsContentUtils::HaveEqualPrincipals(previousFullscreenDoc, this)) {
    RefPtr<AsyncEventDispatcher> asyncDispatcher =
      new AsyncEventDispatcher(this,
                               NS_LITERAL_STRING("MozEnteredDomFullscreen"),
                               true,
                               true);
    asyncDispatcher->PostDOMEvent();
  }

#ifdef DEBUG
  // Note assertions must run before SetWindowFullScreen() as that does
  // synchronous event dispatch which can run script which exits full-screen!
  NS_ASSERTION(GetFullScreenElement() == aElement,
               "Full-screen element should be the requested element!");
  NS_ASSERTION(IsFullScreenDoc(), "Should be full-screen doc");
  nsCOMPtr<nsIDOMElement> fse;
  GetMozFullScreenElement(getter_AddRefs(fse));
  nsCOMPtr<nsIContent> c(do_QueryInterface(fse));
  NS_ASSERTION(c->AsElement() == aElement,
    "GetMozFullScreenElement should match GetFullScreenElement()");
#endif

  // The origin which is fullscreen changed, send a notifiction so that the
  // root document knows the origin of the document which requested fullscreen.
  // This is used for the fullscreen approval UI. If we're in a child
  // process, the root BrowserElementChild listens for this notification,
  // and forwards it across to its BrowserElementParent, which
  // re-broadcasts the message for the root document in its process.
  if (aNotifyOnOriginChange &&
      !nsContentUtils::HaveEqualPrincipals(previousFullscreenDoc, this)) {
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    nsIDocument* root = GetFullscreenRootDocument(this);
    nsAutoString origin;
    nsContentUtils::GetUTFOrigin(NodePrincipal(), origin);
    os->NotifyObservers(root, "fullscreen-origin-change", origin.get());
  }

  // Make the window full-screen. Note we must make the state changes above
  // before making the window full-screen, as then the document reports as
  // being in full-screen mode when the chrome "fullscreen" event fires,
  // enabling chrome to distinguish between browser and dom full-screen
  // modes. Also note that nsGlobalWindow::SetFullScreen() (which
  // SetWindowFullScreen() calls) proxies to the root window in its hierarchy,
  // and does not operate on the a per-nsIDOMWindow basis.
  SetWindowFullScreen(this, true, aOptions.mVRHMDDevice);
}

NS_IMETHODIMP
nsDocument::GetMozFullScreenElement(nsIDOMElement **aFullScreenElement)
{
  ErrorResult rv;
  Element* el = GetMozFullScreenElement(rv);
  if (rv.Failed()) {
    return rv.StealNSResult();
  }
  nsCOMPtr<nsIDOMElement> retval = do_QueryInterface(el);
  retval.forget(aFullScreenElement);
  return NS_OK;
}

Element*
nsDocument::GetMozFullScreenElement(ErrorResult& rv)
{
  if (IsFullScreenDoc()) {
    // Must have a full-screen element while in full-screen mode.
    Element* el = GetFullScreenElement();
    if (!el) {
      rv.Throw(NS_ERROR_UNEXPECTED);
    }
    return el;
  }
  return nullptr;
}

Element*
nsDocument::GetFullScreenElement()
{
  Element* element = FullScreenStackTop();
  NS_ASSERTION(!element ||
               element->IsFullScreenAncestor(),
    "Fullscreen element should have fullscreen styles applied");
  return element;
}

NS_IMETHODIMP
nsDocument::GetMozFullScreen(bool *aFullScreen)
{
  *aFullScreen = MozFullScreen();
  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetMozFullScreenEnabled(bool *aFullScreen)
{
  NS_ENSURE_ARG_POINTER(aFullScreen);
  *aFullScreen = MozFullScreenEnabled();
  return NS_OK;
}

bool
nsDocument::MozFullScreenEnabled()
{
  return IsFullScreenEnabled(nsContentUtils::IsCallerChrome(), false);
}

static bool
HasFullScreenSubDocument(nsIDocument* aDoc)
{
  uint32_t count = CountFullscreenSubDocuments(aDoc);
  NS_ASSERTION(count <= 1, "Fullscreen docs should have at most 1 fullscreen child!");
  return count >= 1;
}

bool
nsDocument::IsFullScreenEnabled(bool aCallerIsChrome, bool aLogFailure)
{
  if (nsContentUtils::IsFullScreenApiEnabled() && aCallerIsChrome) {
    // Chrome code can always use the full-screen API, provided it's not
    // explicitly disabled. Note IsCallerChrome() returns true when running
    // in an Runnable, so don't use GetMozFullScreenEnabled() from an
    // Runnable!
    return true;
  }

  if (!nsContentUtils::IsFullScreenApiEnabled()) {
    LogFullScreenDenied(aLogFailure, "FullScreenDeniedDisabled", this);
    return false;
  }
  if (!IsVisible()) {
    LogFullScreenDenied(aLogFailure, "FullScreenDeniedHidden", this);
    return false;
  }
  if (HasFullScreenSubDocument(this)) {
    LogFullScreenDenied(aLogFailure, "FullScreenDeniedSubDocFullScreen", this);
    return false;
  }

  // Ensure that all containing elements are <iframe> and have
  // allowfullscreen attribute set.
  nsCOMPtr<nsIDocShell> docShell(mDocumentContainer);
  if (!docShell || !docShell->GetFullscreenAllowed()) {
    LogFullScreenDenied(aLogFailure, "FullScreenDeniedContainerNotAllowed", this);
    return false;
  }

  return true;
}

uint16_t
nsDocument::CurrentOrientationAngle() const
{
  return mCurrentOrientationAngle;
}

OrientationType
nsDocument::CurrentOrientationType() const
{
  return mCurrentOrientationType;
}

void
nsDocument::SetCurrentOrientation(mozilla::dom::OrientationType aType,
                                  uint16_t aAngle)
{
  mCurrentOrientationType = aType;
  mCurrentOrientationAngle = aAngle;
}

Promise*
nsDocument::GetOrientationPendingPromise() const
{
  return mOrientationPendingPromise;
}

void
nsDocument::SetOrientationPendingPromise(Promise* aPromise)
{
  mOrientationPendingPromise = aPromise;
}

static void
DispatchPointerLockChange(nsIDocument* aTarget)
{
  if (!aTarget) {
    return;
  }

  RefPtr<AsyncEventDispatcher> asyncDispatcher =
    new AsyncEventDispatcher(aTarget,
                             NS_LITERAL_STRING("mozpointerlockchange"),
                             true,
                             false);
  asyncDispatcher->PostDOMEvent();
}

static void
DispatchPointerLockError(nsIDocument* aTarget)
{
  if (!aTarget) {
    return;
  }

  RefPtr<AsyncEventDispatcher> asyncDispatcher =
    new AsyncEventDispatcher(aTarget,
                             NS_LITERAL_STRING("mozpointerlockerror"),
                             true,
                             false);
  asyncDispatcher->PostDOMEvent();
}

static const uint8_t kPointerLockRequestLimit = 2;

mozilla::StaticRefPtr<nsPointerLockPermissionRequest> gPendingPointerLockRequest;

class nsPointerLockPermissionRequest : public Runnable,
                                       public nsIContentPermissionRequest
{
public:
  nsPointerLockPermissionRequest(Element* aElement, bool aUserInputOrChromeCaller)
  : mElement(do_GetWeakReference(aElement)),
    mDocument(do_GetWeakReference(aElement->OwnerDoc())),
    mUserInputOrChromeCaller(aUserInputOrChromeCaller)
  {
    nsCOMPtr<nsIDocument> doc = do_QueryReferent(mDocument);
    if (doc && doc->GetInnerWindow()) {
      mRequester = new nsContentPermissionRequester(doc->GetInnerWindow());
    }
  }

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSICONTENTPERMISSIONREQUEST

  NS_IMETHOD Run() override
  {
    nsCOMPtr<Element> e = do_QueryReferent(mElement);
    nsCOMPtr<nsIDocument> d = do_QueryReferent(mDocument);
    if (!e || !d || gPendingPointerLockRequest != this ||
        e->GetUncomposedDoc() != d) {
      Handled();
      DispatchPointerLockError(d);
      return NS_OK;
    }

    // We're about to enter fullscreen mode.
    nsDocument* doc = static_cast<nsDocument*>(d.get());
    if (doc->mAsyncFullscreenPending ||
        (doc->mHasFullscreenApprovedObserver && !doc->mIsApprovedForFullscreen)) {
      // We're still waiting for approval.
      return NS_OK;
    }

    if (doc->mIsApprovedForFullscreen || doc->mAllowRelocking) {
      Allow(JS::UndefinedHandleValue);
      return NS_OK;
    }

    // In non-fullscreen mode user input (or chrome caller) is required!
    // Also, don't let the page to try to get the permission too many times.
    if (!mUserInputOrChromeCaller ||
        doc->mCancelledPointerLockRequests > kPointerLockRequestLimit) {
      Handled();
      DispatchPointerLockError(d);
      return NS_OK;
    }

    // Handling a request from user input in non-fullscreen mode.
    // Do a normal permission check.
    nsCOMPtr<nsPIDOMWindow> window = doc->GetInnerWindow();
    nsContentPermissionUtils::AskPermission(this, window);
    return NS_OK;
  }

  void Handled()
  {
    mElement = nullptr;
    mDocument = nullptr;
    if (gPendingPointerLockRequest == this) {
      gPendingPointerLockRequest = nullptr;
    }
  }

  nsWeakPtr mElement;
  nsWeakPtr mDocument;
  bool mUserInputOrChromeCaller;

protected:
  virtual ~nsPointerLockPermissionRequest() {}
  nsCOMPtr<nsIContentPermissionRequester> mRequester;
};

NS_IMPL_ISUPPORTS_INHERITED(nsPointerLockPermissionRequest,
                            Runnable,
                            nsIContentPermissionRequest)

NS_IMETHODIMP
nsPointerLockPermissionRequest::GetTypes(nsIArray** aTypes)
{
  nsTArray<nsString> emptyOptions;
  return nsContentPermissionUtils::CreatePermissionArray(NS_LITERAL_CSTRING("pointerLock"),
                                                         NS_LITERAL_CSTRING("unused"),
                                                         emptyOptions,
                                                         aTypes);
}

NS_IMETHODIMP
nsPointerLockPermissionRequest::GetPrincipal(nsIPrincipal** aPrincipal)
{
  nsCOMPtr<nsIDocument> d = do_QueryReferent(mDocument);
  if (d) {
    NS_ADDREF(*aPrincipal = d->NodePrincipal());
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPointerLockPermissionRequest::GetWindow(nsIDOMWindow** aWindow)
{
  nsCOMPtr<nsIDocument> d = do_QueryReferent(mDocument);
  if (d) {
    NS_IF_ADDREF(*aWindow = d->GetInnerWindow());
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPointerLockPermissionRequest::GetElement(nsIDOMElement** aElement)
{
  // It is enough to implement GetWindow.
  *aElement = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsPointerLockPermissionRequest::Cancel()
{
  nsCOMPtr<nsIDocument> d = do_QueryReferent(mDocument);
  Handled();
  if (d) {
    auto doc = static_cast<nsDocument*>(d.get());
    if (doc->mCancelledPointerLockRequests <= kPointerLockRequestLimit) {
      doc->mCancelledPointerLockRequests++;
    }
    DispatchPointerLockError(d);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPointerLockPermissionRequest::Allow(JS::HandleValue aChoices)
{
  MOZ_ASSERT(aChoices.isUndefined());

  nsCOMPtr<Element> e = do_QueryReferent(mElement);
  nsCOMPtr<nsIDocument> doc = do_QueryReferent(mDocument);
  nsDocument* d = static_cast<nsDocument*>(doc.get());
  if (!e || !d || gPendingPointerLockRequest != this ||
      e->GetUncomposedDoc() != d ||
      (!mUserInputOrChromeCaller && !d->mIsApprovedForFullscreen)) {
    Handled();
    DispatchPointerLockError(d);
    return NS_OK;
  }

  // Mark handled here so that we don't need to call it everywhere below.
  Handled();

  nsCOMPtr<Element> pointerLockedElement =
    do_QueryReferent(EventStateManager::sPointerLockedElement);
  if (e == pointerLockedElement) {
    DispatchPointerLockChange(d);
    return NS_OK;
  }

  // Note, we must bypass focus change, so pass true as the last parameter!
  if (!d->ShouldLockPointer(e, pointerLockedElement, true)) {
    DispatchPointerLockError(d);
    return NS_OK;
  }

  if (!d->SetPointerLock(e, NS_STYLE_CURSOR_NONE)) {
    DispatchPointerLockError(d);
    return NS_OK;
  }

  d->mCancelledPointerLockRequests = 0;
  e->SetPointerLock();
  EventStateManager::sPointerLockedElement = do_GetWeakReference(e);
  EventStateManager::sPointerLockedDoc = do_GetWeakReference(doc);
  NS_ASSERTION(EventStateManager::sPointerLockedElement &&
               EventStateManager::sPointerLockedDoc,
               "aElement and this should support weak references!");

  DispatchPointerLockChange(d);
  return NS_OK;
}

NS_IMETHODIMP
nsPointerLockPermissionRequest::GetRequester(nsIContentPermissionRequester** aRequester)
{
  NS_ENSURE_ARG_POINTER(aRequester);

  nsCOMPtr<nsIContentPermissionRequester> requester = mRequester;
  requester.forget(aRequester);
  return NS_OK;
}

void
nsDocument::SetApprovedForFullscreen(bool aIsApproved)
{
  mIsApprovedForFullscreen = aIsApproved;
}

nsresult
nsDocument::Observe(nsISupports *aSubject,
                    const char *aTopic,
                    const char16_t *aData)
{
  if (strcmp("fullscreen-approved", aTopic) == 0) {
    nsCOMPtr<nsIDocument> subject(do_QueryInterface(aSubject));
    if (subject != this) {
      return NS_OK;
    }
    SetApprovedForFullscreen(true);
    if (gPendingPointerLockRequest) {
      // We have a request pending. Create a clone of it and re-dispatch so that
      // Run() method gets called again.
      nsCOMPtr<Element> el =
        do_QueryReferent(gPendingPointerLockRequest->mElement);
      nsCOMPtr<nsIDocument> doc =
        do_QueryReferent(gPendingPointerLockRequest->mDocument);
      bool userInputOrChromeCaller =
        gPendingPointerLockRequest->mUserInputOrChromeCaller;
      gPendingPointerLockRequest->Handled();
      if (doc == this && el && el->GetUncomposedDoc() == doc) {
        nsPointerLockPermissionRequest* clone =
          new nsPointerLockPermissionRequest(el, userInputOrChromeCaller);
        gPendingPointerLockRequest = clone;
        nsCOMPtr<nsIRunnable> r = gPendingPointerLockRequest.get();
        NS_DispatchToMainThread(r);
      }
    }
  } else if (strcmp("app-theme-changed", aTopic) == 0) {
    if (!nsContentUtils::IsSystemPrincipal(NodePrincipal()) &&
        !IsUnstyledDocument()) {
      // We don't want to style the chrome window, only app ones.
      OnAppThemeChanged();
    }
  } else if (strcmp("service-worker-get-client", aTopic) == 0) {
    // No need to generate the ID if it doesn't exist here.  The ID being
    // requested must already be generated in order to passed in as
    // aSubject.
    nsString clientId = GetId();
    if (!clientId.IsEmpty() && clientId.Equals(aData)) {
      nsCOMPtr<nsISupportsInterfacePointer> ifptr = do_QueryInterface(aSubject);
      if (ifptr) {
#ifdef DEBUG
        nsCOMPtr<nsISupports> value;
        MOZ_ALWAYS_SUCCEEDS(ifptr->GetData(getter_AddRefs(value)));
        MOZ_ASSERT(!value);
#endif
        ifptr->SetData(static_cast<nsIDocument*>(this));
        ifptr->SetDataIID(&NS_GET_IID(nsIDocument));
      }
    }
  }
  return NS_OK;
}

void
nsDocument::OnAppThemeChanged()
{
  // Bail out if there is no theme support set up properly.
  auto themeOrigin = Preferences::GetString("b2g.theme.origin");
  if (!themeOrigin || !Preferences::GetBool("dom.mozApps.themable")) {
    return;
  }

  for (int32_t i = 0; i < GetNumberOfStyleSheets(); i++) {
    StyleSheetHandle::RefPtr sheet = GetStyleSheetAt(i);
    if (!sheet) {
      continue;
    }

    nsINode* owningNode = sheet->GetOwnerNode();
    if (!owningNode) {
      continue;
    }
    // Get a DOM stylesheet link to check the href against the theme origin.
    nsIURI* sheetURI = sheet->GetOriginalURI();
    if (!sheetURI) {
      continue;
    }
    nsAutoString sheetOrigin;
    nsContentUtils::GetUTFOrigin(sheetURI, sheetOrigin);
    if (!sheetOrigin.Equals(themeOrigin)) {
      continue;
    }

    // Finally getting a Stylesheet link.
    nsCOMPtr<nsIStyleSheetLinkingElement> link = do_QueryInterface(owningNode);
    if (!link) {
      continue;
    }
    bool willNotify;
    bool isAlternate;
    link->UpdateStyleSheet(nullptr, &willNotify, &isAlternate, true);
  }
}

void
nsDocument::RequestPointerLock(Element* aElement)
{
  NS_ASSERTION(aElement,
    "Must pass non-null element to nsDocument::RequestPointerLock");

  nsCOMPtr<Element> pointerLockedElement =
    do_QueryReferent(EventStateManager::sPointerLockedElement);
  if (aElement == pointerLockedElement) {
    DispatchPointerLockChange(this);
    return;
  }

  if (!ShouldLockPointer(aElement, pointerLockedElement)) {
    DispatchPointerLockError(this);
    return;
  }

  bool userInputOrChromeCaller = EventStateManager::IsHandlingUserInput() ||
                                 nsContentUtils::IsCallerChrome();

  gPendingPointerLockRequest =
    new nsPointerLockPermissionRequest(aElement, userInputOrChromeCaller);
  nsCOMPtr<nsIRunnable> r = gPendingPointerLockRequest.get();
  NS_DispatchToMainThread(r);
}

bool
nsDocument::ShouldLockPointer(Element* aElement, Element* aCurrentLock,
                              bool aNoFocusCheck)
{
  // Check if pointer lock pref is enabled
  if (!Preferences::GetBool("full-screen-api.pointer-lock.enabled")) {
    NS_WARNING("ShouldLockPointer(): Pointer Lock pref not enabled");
    return false;
  }

  if (aCurrentLock && aCurrentLock->OwnerDoc() != aElement->OwnerDoc()) {
    NS_WARNING("ShouldLockPointer(): Existing pointer lock element in a different document");
    return false;
  }

  if (!aElement->IsInUncomposedDoc()) {
    NS_WARNING("ShouldLockPointer(): Element without Document");
    return false;
  }

  if (mSandboxFlags & SANDBOXED_POINTER_LOCK) {
    NS_WARNING("ShouldLockPointer(): Document is sandboxed and doesn't allow pointer-lock");
    return false;
  }

  // Check if the element is in a document with a docshell.
  nsCOMPtr<nsIDocument> ownerDoc = aElement->OwnerDoc();
  if (!ownerDoc->GetContainer()) {
    return false;
  }
  nsCOMPtr<nsPIDOMWindow> ownerWindow = ownerDoc->GetWindow();
  if (!ownerWindow) {
    return false;
  }
  nsCOMPtr<nsPIDOMWindow> ownerInnerWindow = ownerDoc->GetInnerWindow();
  if (!ownerInnerWindow) {
    return false;
  }
  if (ownerWindow->GetCurrentInnerWindow() != ownerInnerWindow) {
    return false;
  }

  nsCOMPtr<nsPIDOMWindow> top = ownerWindow->GetScriptableTop();
  if (!top || !top->GetExtantDoc() || top->GetExtantDoc()->Hidden()) {
    NS_WARNING("ShouldLockPointer(): Top document isn't visible.");
    return false;
  }

  if (!aNoFocusCheck) {
    mozilla::ErrorResult rv;
    if (!top->GetExtantDoc()->HasFocus(rv)) {
      NS_WARNING("ShouldLockPointer(): Top document isn't focused.");
      return false;
    }
  }

  return true;
}

bool
nsDocument::SetPointerLock(Element* aElement, int aCursorStyle)
{
  MOZ_ASSERT(!aElement || aElement->OwnerDoc() == this,
             "We should be either unlocking pointer (aElement is nullptr), "
             "or locking pointer to an element in this document");
#ifdef DEBUG
  if (!aElement) {
    nsCOMPtr<nsIDocument> pointerLockedDoc =
      do_QueryReferent(EventStateManager::sPointerLockedDoc);
    MOZ_ASSERT(pointerLockedDoc == this);
  }
#endif

  nsIPresShell* shell = GetShell();
  if (!shell) {
    NS_WARNING("SetPointerLock(): No PresShell");
    return false;
  }
  nsPresContext* presContext = shell->GetPresContext();
  if (!presContext) {
    NS_WARNING("SetPointerLock(): Unable to get PresContext");
    return false;
  }

  nsCOMPtr<nsIWidget> widget;
  nsIFrame* rootFrame = shell->GetRootFrame();
  if (!NS_WARN_IF(!rootFrame)) {
    widget = rootFrame->GetNearestWidget();
    NS_WARN_IF_FALSE(widget, "SetPointerLock(): Unable to find widget "
                     "in shell->GetRootFrame()->GetNearestWidget();");
    if (aElement && !widget) {
      return false;
    }
  }

  // Hide the cursor and set pointer lock for future mouse events
  RefPtr<EventStateManager> esm = presContext->EventStateManager();
  esm->SetCursor(aCursorStyle, nullptr, false,
                 0.0f, 0.0f, widget, true);
  esm->SetPointerLock(widget, aElement);

  return true;
}

void
nsDocument::UnlockPointer(nsIDocument* aDoc)
{
  if (!EventStateManager::sIsPointerLocked) {
    return;
  }

  nsCOMPtr<nsIDocument> pointerLockedDoc =
    do_QueryReferent(EventStateManager::sPointerLockedDoc);
  if (!pointerLockedDoc || (aDoc && aDoc != pointerLockedDoc)) {
    return;
  }
  nsDocument* doc = static_cast<nsDocument*>(pointerLockedDoc.get());
  if (!doc->SetPointerLock(nullptr, NS_STYLE_CURSOR_AUTO)) {
    return;
  }

  nsCOMPtr<Element> pointerLockedElement =
    do_QueryReferent(EventStateManager::sPointerLockedElement);
  if (pointerLockedElement) {
    pointerLockedElement->ClearPointerLock();
  }

  EventStateManager::sPointerLockedElement = nullptr;
  EventStateManager::sPointerLockedDoc = nullptr;
  static_cast<nsDocument*>(pointerLockedDoc.get())->mAllowRelocking = !!aDoc;
  gPendingPointerLockRequest = nullptr;
  DispatchPointerLockChange(pointerLockedDoc);
}

void
nsIDocument::UnlockPointer(nsIDocument* aDoc)
{
  nsDocument::UnlockPointer(aDoc);
}

NS_IMETHODIMP
nsDocument::MozExitPointerLock()
{
  nsIDocument::MozExitPointerLock();
  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetMozPointerLockElement(nsIDOMElement** aPointerLockedElement)
{
  Element* el = nsIDocument::GetMozPointerLockElement();
  nsCOMPtr<nsIDOMElement> retval = do_QueryInterface(el);
  retval.forget(aPointerLockedElement);
  return NS_OK;
}

Element*
nsIDocument::GetMozPointerLockElement()
{
  nsCOMPtr<Element> pointerLockedElement =
    do_QueryReferent(EventStateManager::sPointerLockedElement);
  if (!pointerLockedElement) {
    return nullptr;
  }

  // Make sure pointer locked element is in the same document.
  nsCOMPtr<nsIDocument> pointerLockedDoc =
    do_QueryReferent(EventStateManager::sPointerLockedDoc);
  if (pointerLockedDoc != this) {
    return nullptr;
  }

  return pointerLockedElement;
}

void
nsDocument::XPCOMShutdown()
{
  gPendingPointerLockRequest = nullptr;
  sProcessingStack.reset();
}

void
nsDocument::UpdateVisibilityState()
{
  dom::VisibilityState oldState = mVisibilityState;
  mVisibilityState = GetVisibilityState();
  if (oldState != mVisibilityState) {
    nsContentUtils::DispatchTrustedEvent(this, static_cast<nsIDocument*>(this),
                                         NS_LITERAL_STRING("visibilitychange"),
                                         /* bubbles = */ true,
                                         /* cancelable = */ false);
    nsContentUtils::DispatchTrustedEvent(this, static_cast<nsIDocument*>(this),
                                         NS_LITERAL_STRING("mozvisibilitychange"),
                                         /* bubbles = */ true,
                                         /* cancelable = */ false);

    EnumerateActivityObservers(NotifyActivityChanged, nullptr);
  }
}

VisibilityState
nsDocument::GetVisibilityState() const
{
  // We have to check a few pieces of information here:
  // 1)  Are we in bfcache (!IsVisible())?  If so, nothing else matters.
  // 2)  Do we have an outer window?  If not, we're hidden.  Note that we don't
  //     want to use GetWindow here because it does weird groveling for windows
  //     in some cases.
  // 3)  Is our outer window background?  If so, we're hidden.
  // Otherwise, we're visible.
  if (!IsVisible() || !mWindow || !mWindow->GetOuterWindow() ||
      mWindow->GetOuterWindow()->IsBackground()) {
    return dom::VisibilityState::Hidden;
  }

  return dom::VisibilityState::Visible;
}

/* virtual */ void
nsDocument::PostVisibilityUpdateEvent()
{
  nsCOMPtr<nsIRunnable> event =
    NewRunnableMethod(this, &nsDocument::UpdateVisibilityState);
  NS_DispatchToMainThread(event);
}

NS_IMETHODIMP
nsDocument::GetMozHidden(bool* aHidden)
{
  *aHidden = MozHidden();
  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetHidden(bool* aHidden)
{
  *aHidden = Hidden();
  return NS_OK;
}

NS_IMETHODIMP
nsDocument::GetMozVisibilityState(nsAString& aState)
{
  WarnOnceAbout(ePrefixedVisibilityAPI);
  return GetVisibilityState(aState);
}

NS_IMETHODIMP
nsDocument::GetVisibilityState(nsAString& aState)
{
  const EnumEntry& entry =
    VisibilityStateValues::strings[static_cast<int>(mVisibilityState)];
  aState.AssignASCII(entry.value, entry.length);
  return NS_OK;
}

/* virtual */ void
nsIDocument::DocAddSizeOfExcludingThis(nsWindowSizes* aWindowSizes) const
{
  aWindowSizes->mDOMOtherSize +=
    nsINode::SizeOfExcludingThis(aWindowSizes->mMallocSizeOf);

  if (mPresShell) {
    mPresShell->AddSizeOfIncludingThis(aWindowSizes->mMallocSizeOf,
                                       &aWindowSizes->mArenaStats,
                                       &aWindowSizes->mLayoutPresShellSize,
                                       &aWindowSizes->mLayoutStyleSetsSize,
                                       &aWindowSizes->mLayoutTextRunsSize,
                                       &aWindowSizes->mLayoutPresContextSize);
  }

  aWindowSizes->mPropertyTablesSize +=
    mPropertyTable.SizeOfExcludingThis(aWindowSizes->mMallocSizeOf);
  for (uint32_t i = 0, count = mExtraPropertyTables.Length();
       i < count; ++i) {
    aWindowSizes->mPropertyTablesSize +=
      mExtraPropertyTables[i]->SizeOfIncludingThis(aWindowSizes->mMallocSizeOf);
  }

  if (EventListenerManager* elm = GetExistingListenerManager()) {
    aWindowSizes->mDOMEventListenersCount += elm->ListenerCount();
  }

  // Measurement of the following members may be added later if DMD finds it
  // is worthwhile:
  // - many!
}

void
nsIDocument::DocAddSizeOfIncludingThis(nsWindowSizes* aWindowSizes) const
{
  aWindowSizes->mDOMOtherSize += aWindowSizes->mMallocSizeOf(this);
  DocAddSizeOfExcludingThis(aWindowSizes);
}

static size_t
SizeOfOwnedSheetArrayExcludingThis(const nsTArray<StyleSheetHandle::RefPtr>& aSheets,
                                   MallocSizeOf aMallocSizeOf)
{
  size_t n = 0;
  n += aSheets.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (StyleSheetHandle sheet : aSheets) {
    if (!sheet->GetOwningDocument()) {
      // Avoid over-reporting shared sheets.
      continue;
    }
    n += sheet->SizeOfIncludingThis(aMallocSizeOf);
  }
  return n;
}

size_t
nsDocument::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const
{
  // This SizeOfExcludingThis() overrides the one from nsINode.  But
  // nsDocuments can only appear at the top of the DOM tree, and we use the
  // specialized DocAddSizeOfExcludingThis() in that case.  So this should never
  // be called.
  MOZ_CRASH();
}

void
nsDocument::DocAddSizeOfExcludingThis(nsWindowSizes* aWindowSizes) const
{
  nsIDocument::DocAddSizeOfExcludingThis(aWindowSizes);

  for (nsIContent* node = nsINode::GetFirstChild();
       node;
       node = node->GetNextNode(this))
  {
    size_t nodeSize = node->SizeOfIncludingThis(aWindowSizes->mMallocSizeOf);
    size_t* p;

    switch (node->NodeType()) {
    case nsIDOMNode::ELEMENT_NODE:
      p = &aWindowSizes->mDOMElementNodesSize;
      break;
    case nsIDOMNode::TEXT_NODE:
      p = &aWindowSizes->mDOMTextNodesSize;
      break;
    case nsIDOMNode::CDATA_SECTION_NODE:
      p = &aWindowSizes->mDOMCDATANodesSize;
      break;
    case nsIDOMNode::COMMENT_NODE:
      p = &aWindowSizes->mDOMCommentNodesSize;
      break;
    default:
      p = &aWindowSizes->mDOMOtherSize;
      break;
    }

    *p += nodeSize;

    if (EventListenerManager* elm = node->GetExistingListenerManager()) {
      aWindowSizes->mDOMEventListenersCount += elm->ListenerCount();
    }
  }

  aWindowSizes->mStyleSheetsSize +=
    SizeOfOwnedSheetArrayExcludingThis(mStyleSheets,
                                       aWindowSizes->mMallocSizeOf);
  // Note that we do not own the sheets pointed to by mOnDemandBuiltInUASheets
  // (the nsLayoutStyleSheetCache singleton does).
  aWindowSizes->mStyleSheetsSize +=
    mOnDemandBuiltInUASheets.ShallowSizeOfExcludingThis(
        aWindowSizes->mMallocSizeOf);
  for (auto& sheetArray : mAdditionalSheets) {
    aWindowSizes->mStyleSheetsSize +=
      SizeOfOwnedSheetArrayExcludingThis(sheetArray,
                                         aWindowSizes->mMallocSizeOf);
  }
  // Lumping in the loader with the style-sheets size is not ideal,
  // but most of the things in there are in fact stylesheets, so it
  // doesn't seem worthwhile to separate it out.
  aWindowSizes->mStyleSheetsSize +=
    CSSLoader()->SizeOfIncludingThis(aWindowSizes->mMallocSizeOf);

  aWindowSizes->mDOMOtherSize +=
    mAttrStyleSheet ?
    mAttrStyleSheet->DOMSizeOfIncludingThis(aWindowSizes->mMallocSizeOf) :
    0;

  aWindowSizes->mDOMOtherSize +=
    mSVGAttrAnimationRuleProcessor ?
    mSVGAttrAnimationRuleProcessor->DOMSizeOfIncludingThis(
                                      aWindowSizes->mMallocSizeOf) :
    0;

  aWindowSizes->mDOMOtherSize +=
    mStyledLinks.ShallowSizeOfExcludingThis(aWindowSizes->mMallocSizeOf);

  aWindowSizes->mDOMOtherSize +=
    mIdentifierMap.SizeOfExcludingThis(aWindowSizes->mMallocSizeOf);

  // Measurement of the following members may be added later if DMD finds it
  // is worthwhile:
  // - many!
}

NS_IMETHODIMP
nsDocument::QuerySelector(const nsAString& aSelector, nsIDOMElement **aReturn)
{
  return nsINode::QuerySelector(aSelector, aReturn);
}

NS_IMETHODIMP
nsDocument::QuerySelectorAll(const nsAString& aSelector, nsIDOMNodeList **aReturn)
{
  return nsINode::QuerySelectorAll(aSelector, aReturn);
}

already_AddRefed<nsIDocument>
nsIDocument::Constructor(const GlobalObject& aGlobal,
                         ErrorResult& rv)
{
  nsCOMPtr<nsIScriptGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  nsCOMPtr<nsIScriptObjectPrincipal> prin = do_QueryInterface(aGlobal.GetAsSupports());
  if (!prin) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "about:blank");
  if (!uri) {
    rv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }

  nsCOMPtr<nsIDOMDocument> document;
  nsresult res =
    NS_NewDOMDocument(getter_AddRefs(document),
                      NullString(),
                      EmptyString(),
                      nullptr,
                      uri,
                      uri,
                      prin->GetPrincipal(),
                      true,
                      global,
                      DocumentFlavorPlain);
  if (NS_FAILED(res)) {
    rv.Throw(res);
    return nullptr;
  }

  nsCOMPtr<nsIDocument> doc = do_QueryInterface(document);
  doc->SetReadyStateInternal(nsIDocument::READYSTATE_COMPLETE);

  return doc.forget();
}

XPathExpression*
nsIDocument::CreateExpression(const nsAString& aExpression,
                              XPathNSResolver* aResolver,
                              ErrorResult& rv)
{
  return XPathEvaluator()->CreateExpression(aExpression, aResolver, rv);
}

nsINode*
nsIDocument::CreateNSResolver(nsINode& aNodeResolver)
{
  return XPathEvaluator()->CreateNSResolver(aNodeResolver);
}

already_AddRefed<XPathResult>
nsIDocument::Evaluate(JSContext* aCx, const nsAString& aExpression,
                      nsINode& aContextNode, XPathNSResolver* aResolver,
                      uint16_t aType, JS::Handle<JSObject*> aResult,
                      ErrorResult& rv)
{
  return XPathEvaluator()->Evaluate(aCx, aExpression, aContextNode, aResolver,
                                    aType, aResult, rv);
}

NS_IMETHODIMP
nsDocument::Evaluate(const nsAString& aExpression, nsIDOMNode* aContextNode,
                     nsIDOMNode* aResolver, uint16_t aType,
                     nsISupports* aInResult, nsISupports** aResult)
{
  return XPathEvaluator()->Evaluate(aExpression, aContextNode, aResolver, aType,
                                    aInResult, aResult);
}

bool
nsIDocument::HasScriptsBlockedBySandbox()
{
  return mSandboxFlags & SANDBOXED_SCRIPTS;
}

nsIDocument*
nsIDocument::GetTopLevelContentDocument()
{
  nsDocument* parent;

  if (!mLoadedAsData) {
    parent = static_cast<nsDocument*>(this);
  } else {
    nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(GetScopeObject());
    if (!window) {
      return nullptr;
    }

    parent = static_cast<nsDocument*>(window->GetExtantDoc());
    if (!parent) {
      return nullptr;
    }
  }

  do {
    if (parent->IsTopLevelContentDocument()) {
      break;
    }

    // If we ever have a non-content parent before we hit a toplevel content
    // parent, then we're never going to find one.  Just bail.
    if (!parent->IsContentDocument()) {
      return nullptr;
    }

    nsIDocument* candidate = parent->GetParentDocument();
    parent = static_cast<nsDocument*>(candidate);
  } while (parent);

  return parent;
}

void
nsIDocument::PropagateUseCounters(nsIDocument* aParentDocument)
{
  MOZ_ASSERT(this != aParentDocument);

  // What really matters here is that our use counters get propagated as
  // high up in the content document hierarchy as possible.  So,
  // starting with aParentDocument, we need to find the toplevel content
  // document, and propagate our use counters into its
  // mChildDocumentUseCounters.
  nsIDocument* contentParent = aParentDocument->GetTopLevelContentDocument();

  if (!contentParent) {
    return;
  }

  contentParent->mChildDocumentUseCounters |= mUseCounters;
  contentParent->mChildDocumentUseCounters |= mChildDocumentUseCounters;
}

void
nsIDocument::SetPageUseCounter(UseCounter aUseCounter)
{
  // We want to set the use counter on the "page" that owns us; the definition
  // of "page" depends on what kind of document we are.  See the comments below
  // for details.  In any event, checking all the conditions below is
  // reasonably expensive, so we cache whether we've notified our owning page.
  if (mNotifiedPageForUseCounter[aUseCounter]) {
    return;
  }
  mNotifiedPageForUseCounter[aUseCounter] = true;

  if (mDisplayDocument) {
    // If we are a resource document, we won't have a docshell and so we won't
    // record any page use counters on this document.  Instead, we should
    // forward it up to the document that loaded us.
    MOZ_ASSERT(!mDocumentContainer);
    mDisplayDocument->SetChildDocumentUseCounter(aUseCounter);
    return;
  }

  if (IsBeingUsedAsImage()) {
    // If this is an SVG image document, we also won't have a docshell.
    MOZ_ASSERT(!mDocumentContainer);
    return;
  }

  // We only care about use counters in content.  If we're already a toplevel
  // content document, then we should have already set the use counter on
  // ourselves, and we are done.
  nsIDocument* contentParent = GetTopLevelContentDocument();
  if (!contentParent) {
    return;
  }

  if (this == contentParent) {
    MOZ_ASSERT(GetUseCounter(aUseCounter));
    return;
  }

  contentParent->SetChildDocumentUseCounter(aUseCounter);
}

static bool
MightBeAboutOrChromeScheme(nsIURI* aURI)
{
  MOZ_ASSERT(aURI);
  bool isAbout = true;
  bool isChrome = true;
  aURI->SchemeIs("about", &isAbout);
  aURI->SchemeIs("chrome", &isChrome);
  return isAbout || isChrome;
}

void
nsDocument::ReportUseCounters()
{
  static const bool sDebugUseCounters = false;
  if (mReportedUseCounters) {
    return;
  }

  mReportedUseCounters = true;

  if (Telemetry::HistogramUseCounterCount > 0 &&
      (IsContentDocument() || IsResourceDoc())) {
    nsCOMPtr<nsIURI> uri;
    NodePrincipal()->GetURI(getter_AddRefs(uri));
    if (!uri || MightBeAboutOrChromeScheme(uri)) {
      return;
    }

    if (sDebugUseCounters) {
      nsCString spec;
      uri->GetSpec(spec);

      // URIs can be rather long for data documents, so truncate them to
      // some reasonable length.
      spec.Truncate(std::min(128U, spec.Length()));
      printf("-- Use counters for %s --\n", spec.get());
    }

    // We keep separate counts for individual documents and top-level
    // pages to more accurately track how many web pages might break if
    // certain features were removed.  Consider the case of a single
    // HTML document with several SVG images and/or iframes with
    // sub-documents of their own.  If we maintained a single set of use
    // counters and all the sub-documents use a particular feature, then
    // telemetry would indicate that we would be breaking N documents if
    // that feature were removed.  Whereas with a document/top-level
    // page split, we can see that N documents would be affected, but
    // only a single web page would be affected.

    for (int32_t c = 0;
         c < eUseCounter_Count; ++c) {
      UseCounter uc = static_cast<UseCounter>(c);

      Telemetry::ID id =
        static_cast<Telemetry::ID>(Telemetry::HistogramFirstUseCounter + uc * 2);
      bool value = GetUseCounter(uc);

      if (value) {
        if (sDebugUseCounters) {
          const char* name = Telemetry::GetHistogramName(id);
          if (name) {
            printf("  %s", name);
          } else {
            printf("  #%d", id);
          }
          printf(": %d\n", value);
        }

        Telemetry::Accumulate(id, 1);
      }

      if (IsTopLevelContentDocument()) {
        id = static_cast<Telemetry::ID>(Telemetry::HistogramFirstUseCounter +
                                        uc * 2 + 1);
        value = GetUseCounter(uc) || GetChildDocumentUseCounter(uc);

        if (value) {
          if (sDebugUseCounters) {
            const char* name = Telemetry::GetHistogramName(id);
            if (name) {
              printf("  %s", name);
            } else {
              printf("  #%d", id);
            }
            printf(": %d\n", value);
          }

          Telemetry::Accumulate(id, 1);
        }
      }
    }
  }
}

XPathEvaluator*
nsIDocument::XPathEvaluator()
{
  if (!mXPathEvaluator) {
    mXPathEvaluator = new dom::XPathEvaluator(this);
  }
  return mXPathEvaluator;
}

already_AddRefed<nsIDocumentEncoder>
nsIDocument::GetCachedEncoder()
{
  return mCachedEncoder.forget();
}

void
nsIDocument::SetCachedEncoder(already_AddRefed<nsIDocumentEncoder> aEncoder)
{
  mCachedEncoder = aEncoder;
}

void
nsIDocument::SetContentTypeInternal(const nsACString& aType)
{
  mCachedEncoder = nullptr;
  mContentType = aType;
}

nsILoadContext*
nsIDocument::GetLoadContext() const
{
  return mDocumentContainer;
}

nsIDocShell*
nsIDocument::GetDocShell() const
{
  return mDocumentContainer;
}

void
nsIDocument::SetStateObject(nsIStructuredCloneContainer *scContainer)
{
  mStateObjectContainer = scContainer;
  mStateObjectCached = nullptr;
}

already_AddRefed<Element>
nsIDocument::CreateHTMLElement(nsIAtom* aTag)
{
  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  nodeInfo = mNodeInfoManager->GetNodeInfo(aTag, nullptr, kNameSpaceID_XHTML,
                                           nsIDOMNode::ELEMENT_NODE);
  MOZ_ASSERT(nodeInfo, "GetNodeInfo should never fail");

  nsCOMPtr<Element> element;
  DebugOnly<nsresult> rv = NS_NewHTMLElement(getter_AddRefs(element),
                                             nodeInfo.forget(),
                                             mozilla::dom::NOT_FROM_PARSER);

  MOZ_ASSERT(NS_SUCCEEDED(rv), "NS_NewHTMLElement should never fail");
  return element.forget();
}

/* static */
nsresult
nsIDocument::GenerateDocumentId(nsAString& aId)
{
  nsID id;
  nsresult rv = nsContentUtils::GenerateUUIDInPlace(id);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Build a string in {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} format
  char buffer[NSID_LENGTH];
  id.ToProvidedString(buffer);
  NS_ConvertASCIItoUTF16 uuid(buffer);

  // Remove {} and the null terminator
  aId.Assign(Substring(uuid, 1, NSID_LENGTH - 3));
  return NS_OK;
}

nsresult
nsIDocument::GetOrCreateId(nsAString& aId)
{
  if (mId.IsEmpty()) {
    nsresult rv = GenerateDocumentId(mId);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  aId = mId;
  return NS_OK;
}

void
nsIDocument::SetId(const nsAString& aId)
{
  // The ID should only be set one time, but we may get the same value
  // more than once if the document is controlled coming out of bfcache.
  MOZ_ASSERT_IF(mId != aId, mId.IsEmpty());
  mId = aId;
}

bool
MarkDocumentTreeToBeInSyncOperation(nsIDocument* aDoc, void* aData)
{
  nsCOMArray<nsIDocument>* documents =
    static_cast<nsCOMArray<nsIDocument>*>(aData);
  if (aDoc) {
    aDoc->SetIsInSyncOperation(true);
    documents->AppendObject(aDoc);
    aDoc->EnumerateSubDocuments(MarkDocumentTreeToBeInSyncOperation, aData);
  }
  return true;
}

nsAutoSyncOperation::nsAutoSyncOperation(nsIDocument* aDoc)
{
  mMicroTaskLevel = nsContentUtils::MicroTaskLevel();
  nsContentUtils::SetMicroTaskLevel(0);
  if (aDoc) {
    nsPIDOMWindow* win = aDoc->GetWindow();
    if (win) {
      if (nsCOMPtr<nsPIDOMWindow> top = win->GetTop()) {
        nsCOMPtr<nsIDocument> doc = top->GetExtantDoc();
        MarkDocumentTreeToBeInSyncOperation(doc, &mDocuments);
      }
    }
  }
}

nsAutoSyncOperation::~nsAutoSyncOperation()
{
  for (int32_t i = 0; i < mDocuments.Count(); ++i) {
    mDocuments[i]->SetIsInSyncOperation(false);
  }
  nsContentUtils::SetMicroTaskLevel(mMicroTaskLevel);
}

gfxUserFontSet*
nsIDocument::GetUserFontSet()
{
  // We want to initialize the user font set lazily the first time the
  // user asks for it, rather than building it too early and forcing
  // rule cascade creation.  Thus we try to enforce the invariant that
  // we *never* build the user font set until the first call to
  // GetUserFontSet.  However, once it's been requested, we can't wait
  // for somebody to call GetUserFontSet in order to rebuild it (see
  // comments below in RebuildUserFontSet for why).
#ifdef DEBUG
  bool userFontSetGottenBefore = mGetUserFontSetCalled;
#endif
  // Set mGetUserFontSetCalled up front, so that FlushUserFontSet will actually
  // flush.
  mGetUserFontSetCalled = true;
  if (mFontFaceSetDirty) {
    // If this assertion fails, and there have actually been changes to
    // @font-face rules, then we will call StyleChangeReflow in
    // FlushUserFontSet.  If we're in the middle of reflow,
    // that's a bad thing to do, and the caller was responsible for
    // flushing first.  If we're not (e.g., in frame construction), it's
    // ok.
    NS_ASSERTION(!userFontSetGottenBefore ||
                 !GetShell() ||
                 !GetShell()->IsReflowLocked(),
                 "FlushUserFontSet should have been called first");
    FlushUserFontSet();
  }

  if (!mFontFaceSet) {
    return nullptr;
  }

  return mFontFaceSet->GetUserFontSet();
}

void
nsIDocument::FlushUserFontSet()
{
  if (!mGetUserFontSetCalled) {
    return; // No one cares about this font set yet, but we want to be careful
            // to not unset our mFontFaceSetDirty bit, so when someone really
            // does we'll create it.
  }

  if (mFontFaceSetDirty) {
    if (gfxPlatform::GetPlatform()->DownloadableFontsEnabled()) {
      nsTArray<nsFontFaceRuleContainer> rules;
      nsIPresShell* shell = GetShell();
      if (shell) {
        // XXXheycam ServoStyleSets don't support exposing @font-face rules yet.
        if (shell->StyleSet()->IsGecko()) {
          if (!shell->StyleSet()->AsGecko()->AppendFontFaceRules(rules)) {
            return;
          }
        } else {
          NS_ERROR("stylo: ServoStyleSets cannot handle @font-face rules yet");
        }
      }

      bool changed = false;

      if (!mFontFaceSet && !rules.IsEmpty()) {
        nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(GetScopeObject());
        mFontFaceSet = new FontFaceSet(window, this);
      }
      if (mFontFaceSet) {
        changed = mFontFaceSet->UpdateRules(rules);
      }

      // We need to enqueue a style change reflow (for later) to
      // reflect that we're modifying @font-face rules.  (However,
      // without a reflow, nothing will happen to start any downloads
      // that are needed.)
      if (changed && shell) {
        nsPresContext* presContext = shell->GetPresContext();
        if (presContext) {
          presContext->UserFontSetUpdated();
        }
      }
    }

    mFontFaceSetDirty = false;
  }
}

void
nsIDocument::RebuildUserFontSet()
{
  if (!mGetUserFontSetCalled) {
    // We want to lazily build the user font set the first time it's
    // requested (so we don't force creation of rule cascades too
    // early), so don't do anything now.
    return;
  }

  mFontFaceSetDirty = true;
  SetNeedStyleFlush();

  // Somebody has already asked for the user font set, so we need to
  // post an event to rebuild it.  Setting the user font set to be dirty
  // and lazily rebuilding it isn't sufficient, since it is only the act
  // of rebuilding it that will trigger the style change reflow that
  // calls GetUserFontSet.  (This reflow causes rebuilding of text runs,
  // which starts font loads, whose completion causes another style
  // change reflow).
  if (!mPostedFlushUserFontSet) {
    nsCOMPtr<nsIRunnable> ev =
      NewRunnableMethod(this, &nsIDocument::HandleRebuildUserFontSet);
    if (NS_SUCCEEDED(NS_DispatchToCurrentThread(ev))) {
      mPostedFlushUserFontSet = true;
    }
  }
}

FontFaceSet*
nsIDocument::Fonts()
{
  if (!mFontFaceSet) {
    nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(GetScopeObject());
    mFontFaceSet = new FontFaceSet(window, this);
    GetUserFontSet();  // this will cause the user font set to be created/updated
  }
  return mFontFaceSet;
}

void
nsIDocument::ReportHasScrollLinkedEffect()
{
  if (mHasScrollLinkedEffect) {
    // We already did this once for this document, don't do it again.
    return;
  }
  mHasScrollLinkedEffect = true;
  nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                  NS_LITERAL_CSTRING("Async Pan/Zoom"),
                                  this, nsContentUtils::eLAYOUT_PROPERTIES,
                                  "ScrollLinkedEffectFound2");
}

mozilla::StyleBackendType
nsIDocument::GetStyleBackendType() const
{
  if (!mPresShell) {
#ifdef MOZ_STYLO
    NS_WARNING("GetStyleBackendType() called on document without a pres shell");
#endif
    return StyleBackendType::Gecko;
  }
  return mPresShell->StyleSet()->BackendType();
}

Selection*
nsIDocument::GetSelection(ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindow> window = GetInnerWindow();
  if (!window) {
    return nullptr;
  }

  NS_ASSERTION(window->IsInnerWindow(), "Should have inner window here!");
  if (!window->IsCurrentInnerWindow()) {
    return nullptr;
  }

  return static_cast<nsGlobalWindow*>(window.get())->GetSelection(aRv);
}
