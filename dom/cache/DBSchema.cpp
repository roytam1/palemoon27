/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/cache/DBSchema.h"

#include "ipc/IPCMessageUtils.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/cache/PCacheTypes.h"
#include "mozilla/dom/cache/SavedTypes.h"
#include "mozIStorageConnection.h"
#include "mozIStorageStatement.h"
#include "nsCOMPtr.h"
#include "nsTArray.h"

namespace mozilla {
namespace dom {
namespace cache {


const int32_t DBSchema::kMaxWipeSchemaVersion = 2;
const int32_t DBSchema::kLatestSchemaVersion = 2;
const int32_t DBSchema::kMaxEntriesPerStatement = 255;

using mozilla::void_t;

// static
nsresult
DBSchema::CreateSchema(mozIStorageConnection* aConn)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);

  nsAutoCString pragmas(
#if defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_GONK)
    // Switch the journaling mode to TRUNCATE to avoid changing the directory
    // structure at the conclusion of every transaction for devices with slower
    // file systems.
    "PRAGMA journal_mode = TRUNCATE; "
#endif
    "PRAGMA foreign_keys = ON; "

    // Note, the default encoding of UTF-8 is preferred.  mozStorage does all
    // the work necessary to convert UTF-16 nsString values for us.  We don't
    // need ordering and the binary equality operations are correct.  So, do
    // NOT set PRAGMA encoding to UTF-16.
  );

  nsresult rv = aConn->ExecuteSimpleSQL(pragmas);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  int32_t schemaVersion;
  rv = aConn->GetSchemaVersion(&schemaVersion);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  if (schemaVersion == kLatestSchemaVersion) {
    return rv;
  }

  if (!schemaVersion) {
    // The caches table is the single source of truth about what Cache
    // objects exist for the origin.  The contents of the Cache are stored
    // in the entries table that references back to caches.
    //
    // The caches table is also referenced from storage.  Rows in storage
    // represent named Cache objects.  There are cases, however, where
    // a Cache can still exist, but not be in a named Storage.  For example,
    // when content is still using the Cache after CacheStorage::Delete()
    // has been run.
    //
    // For now, the caches table mainly exists for data integrity with
    // foreign keys, but could be expanded to contain additional cache object
    // information.
    rv = aConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "CREATE TABLE caches ("
        "id INTEGER NOT NULL PRIMARY KEY "
      ");"
    ));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = aConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "CREATE TABLE entries ("
        "id INTEGER NOT NULL PRIMARY KEY, "
        "request_method TEXT NOT NULL, "
        "request_url TEXT NOT NULL, "
        "request_url_no_query TEXT NOT NULL, "
        "request_referrer TEXT NOT NULL, "
        "request_headers_guard INTEGER NOT NULL, "
        "request_mode INTEGER NOT NULL, "
        "request_credentials INTEGER NOT NULL, "
        "request_body_id TEXT NULL, "
        "response_type INTEGER NOT NULL, "
        "response_url TEXT NOT NULL, "
        "response_status INTEGER NOT NULL, "
        "response_status_text TEXT NOT NULL, "
        "response_headers_guard INTEGER NOT NULL, "
        "response_body_id TEXT NULL, "
        "response_security_info BLOB NULL, "
        "cache_id INTEGER NOT NULL REFERENCES caches(id) ON DELETE CASCADE"
      ");"
    ));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    // TODO: see if we can remove these indices on TEXT columns (bug 1110458)
    rv = aConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "CREATE INDEX entries_request_url_index "
                "ON entries (request_url);"
    ));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = aConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "CREATE INDEX entries_request_url_no_query_index "
                "ON entries (request_url_no_query);"
    ));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = aConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "CREATE TABLE request_headers ("
        "name TEXT NOT NULL, "
        "value TEXT NOT NULL, "
        "entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE"
      ");"
    ));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = aConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "CREATE TABLE response_headers ("
        "name TEXT NOT NULL, "
        "value TEXT NOT NULL, "
        "entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE"
      ");"
    ));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    // We need an index on response_headers, but not on request_headers,
    // because we quickly need to determine if a VARY header is present.
    rv = aConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "CREATE INDEX response_headers_name_index "
                "ON response_headers (name);"
    ));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = aConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
      "CREATE TABLE storage ("
        "namespace INTEGER NOT NULL, "
        "key TEXT NOT NULL, "
        "cache_id INTEGER NOT NULL REFERENCES caches(id), "
        "PRIMARY KEY(namespace, key) "
      ");"
    ));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = aConn->SetSchemaVersion(kLatestSchemaVersion);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = aConn->GetSchemaVersion(&schemaVersion);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  }

  if (schemaVersion != kLatestSchemaVersion) {
    return NS_ERROR_FAILURE;
  }

  return rv;
}

// static
nsresult
DBSchema::CreateCache(mozIStorageConnection* aConn, CacheId* aCacheIdOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);
  MOZ_ASSERT(aCacheIdOut);

  nsresult rv = aConn->ExecuteSimpleSQL(NS_LITERAL_CSTRING(
    "INSERT INTO caches DEFAULT VALUES;"
  ));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  nsCOMPtr<mozIStorageStatement> state;
  rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT last_insert_rowid()"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool hasMoreData = false;
  rv = state->ExecuteStep(&hasMoreData);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  if (NS_WARN_IF(!hasMoreData)) { return NS_ERROR_UNEXPECTED; }

  rv = state->GetInt32(0, aCacheIdOut);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  return rv;
}

// static
nsresult
DBSchema::DeleteCache(mozIStorageConnection* aConn, CacheId aCacheId,
                      nsTArray<nsID>& aDeletedBodyIdListOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);

  // Delete the bodies explicitly as we need to read out the body IDs
  // anyway.  These body IDs must be deleted one-by-one as content may
  // still be referencing them invidivually.
  nsAutoTArray<EntryId, 256> matches;
  nsresult rv = QueryAll(aConn, aCacheId, matches);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = DeleteEntries(aConn, matches, aDeletedBodyIdListOut);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  // Delete the remainder of the cache using cascade semantics.
  nsCOMPtr<mozIStorageStatement> state;
  rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "DELETE FROM caches WHERE id=?1;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aCacheId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->Execute();
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  return rv;
}

// static
nsresult
DBSchema::IsCacheOrphaned(mozIStorageConnection* aConn,
                          CacheId aCacheId, bool* aOrphanedOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);
  MOZ_ASSERT(aOrphanedOut);

  // err on the side of not deleting user data
  *aOrphanedOut = false;

  nsCOMPtr<mozIStorageStatement> state;
  nsresult rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT COUNT(*) FROM storage WHERE cache_id=?1;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aCacheId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool hasMoreData = false;
  rv = state->ExecuteStep(&hasMoreData);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  MOZ_ASSERT(hasMoreData);

  int32_t refCount;
  rv = state->GetInt32(0, &refCount);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  *aOrphanedOut = refCount == 0;

  return rv;
}

// static
nsresult
DBSchema::CacheMatch(mozIStorageConnection* aConn, CacheId aCacheId,
                     const PCacheRequest& aRequest,
                     const PCacheQueryParams& aParams,
                     bool* aFoundResponseOut,
                     SavedResponse* aSavedResponseOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);
  MOZ_ASSERT(aFoundResponseOut);
  MOZ_ASSERT(aSavedResponseOut);

  *aFoundResponseOut = false;

  nsAutoTArray<EntryId, 1> matches;
  nsresult rv = QueryCache(aConn, aCacheId, aRequest, aParams, matches, 1);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  if (matches.IsEmpty()) {
    return rv;
  }

  rv = ReadResponse(aConn, matches[0], aSavedResponseOut);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  aSavedResponseOut->mCacheId = aCacheId;
  *aFoundResponseOut = true;

  return rv;
}

// static
nsresult
DBSchema::CacheMatchAll(mozIStorageConnection* aConn, CacheId aCacheId,
                        const PCacheRequestOrVoid& aRequestOrVoid,
                        const PCacheQueryParams& aParams,
                        nsTArray<SavedResponse>& aSavedResponsesOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);
  nsresult rv;

  nsAutoTArray<EntryId, 256> matches;
  if (aRequestOrVoid.type() == PCacheRequestOrVoid::Tvoid_t) {
    rv = QueryAll(aConn, aCacheId, matches);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  } else {
    rv = QueryCache(aConn, aCacheId, aRequestOrVoid, aParams, matches);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  }

  // TODO: replace this with a bulk load using SQL IN clause (bug 1110458)
  for (uint32_t i = 0; i < matches.Length(); ++i) {
    SavedResponse savedResponse;
    rv = ReadResponse(aConn, matches[i], &savedResponse);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
    savedResponse.mCacheId = aCacheId;
    aSavedResponsesOut.AppendElement(savedResponse);
  }

  return rv;
}

// static
nsresult
DBSchema::CachePut(mozIStorageConnection* aConn, CacheId aCacheId,
                   const PCacheRequest& aRequest,
                   const nsID* aRequestBodyId,
                   const PCacheResponse& aResponse,
                   const nsID* aResponseBodyId,
                   nsTArray<nsID>& aDeletedBodyIdListOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);

  PCacheQueryParams params(false, false, false, false, false,
                           NS_LITERAL_STRING(""));
  nsAutoTArray<EntryId, 256> matches;
  nsresult rv = QueryCache(aConn, aCacheId, aRequest, params, matches);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = DeleteEntries(aConn, matches, aDeletedBodyIdListOut);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = InsertEntry(aConn, aCacheId, aRequest, aRequestBodyId, aResponse,
                   aResponseBodyId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  return rv;
}

// static
nsresult
DBSchema::CacheDelete(mozIStorageConnection* aConn, CacheId aCacheId,
                      const PCacheRequest& aRequest,
                      const PCacheQueryParams& aParams,
                      nsTArray<nsID>& aDeletedBodyIdListOut, bool* aSuccessOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);
  MOZ_ASSERT(aSuccessOut);

  *aSuccessOut = false;

  nsAutoTArray<EntryId, 256> matches;
  nsresult rv = QueryCache(aConn, aCacheId, aRequest, aParams, matches);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  if (matches.IsEmpty()) {
    return rv;
  }

  rv = DeleteEntries(aConn, matches, aDeletedBodyIdListOut);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  *aSuccessOut = true;

  return rv;
}

// static
nsresult
DBSchema::CacheKeys(mozIStorageConnection* aConn, CacheId aCacheId,
                    const PCacheRequestOrVoid& aRequestOrVoid,
                    const PCacheQueryParams& aParams,
                    nsTArray<SavedRequest>& aSavedRequestsOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);
  nsresult rv;

  nsAutoTArray<EntryId, 256> matches;
  if (aRequestOrVoid.type() == PCacheRequestOrVoid::Tvoid_t) {
    rv = QueryAll(aConn, aCacheId, matches);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  } else {
    rv = QueryCache(aConn, aCacheId, aRequestOrVoid, aParams, matches);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  }

  // TODO: replace this with a bulk load using SQL IN clause (bug 1110458)
  for (uint32_t i = 0; i < matches.Length(); ++i) {
    SavedRequest savedRequest;
    rv = ReadRequest(aConn, matches[i], &savedRequest);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
    savedRequest.mCacheId = aCacheId;
    aSavedRequestsOut.AppendElement(savedRequest);
  }

  return rv;
}

// static
nsresult
DBSchema::StorageMatch(mozIStorageConnection* aConn,
                       Namespace aNamespace,
                       const PCacheRequest& aRequest,
                       const PCacheQueryParams& aParams,
                       bool* aFoundResponseOut,
                       SavedResponse* aSavedResponseOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);
  MOZ_ASSERT(aFoundResponseOut);
  MOZ_ASSERT(aSavedResponseOut);

  *aFoundResponseOut = false;

  nsresult rv;

  // If we are given a cache to check, then simply find its cache ID
  // and perform the match.
  if (!aParams.cacheName().EqualsLiteral("")) {
    bool foundCache = false;
    // no invalid CacheId, init to least likely real value
    CacheId cacheId = INT32_MAX;
    rv = StorageGetCacheId(aConn, aNamespace, aParams.cacheName(), &foundCache,
                           &cacheId);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
    if (!foundCache) { return NS_ERROR_DOM_NOT_FOUND_ERR; }

    rv = CacheMatch(aConn, cacheId, aRequest, aParams, aFoundResponseOut,
                    aSavedResponseOut);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    return rv;
  }

  // Otherwise we need to get a list of all the cache IDs in this namespace.

  nsCOMPtr<mozIStorageStatement> state;
  rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT cache_id FROM storage WHERE namespace=?1 ORDER BY rowid;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aNamespace);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  nsAutoTArray<CacheId, 32> cacheIdList;

  bool hasMoreData = false;
  while (NS_SUCCEEDED(state->ExecuteStep(&hasMoreData)) && hasMoreData) {
    CacheId cacheId = INT32_MAX;
    rv = state->GetInt32(0, &cacheId);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
    cacheIdList.AppendElement(cacheId);
  }

  // Now try to find a match in each cache in order
  for (uint32_t i = 0; i < cacheIdList.Length(); ++i) {
    rv = CacheMatch(aConn, cacheIdList[i], aRequest, aParams, aFoundResponseOut,
                    aSavedResponseOut);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    if (*aFoundResponseOut) {
      aSavedResponseOut->mCacheId = cacheIdList[i];
      return rv;
    }
  }

  return NS_OK;
}

// static
nsresult
DBSchema::StorageGetCacheId(mozIStorageConnection* aConn, Namespace aNamespace,
                            const nsAString& aKey, bool* aFoundCacheOut,
                            CacheId* aCacheIdOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);
  MOZ_ASSERT(aFoundCacheOut);
  MOZ_ASSERT(aCacheIdOut);

  *aFoundCacheOut = false;

  nsCOMPtr<mozIStorageStatement> state;
  nsresult rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT cache_id FROM storage WHERE namespace=?1 AND key=?2 ORDER BY rowid;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aNamespace);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindStringParameter(1, aKey);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool hasMoreData = false;
  rv = state->ExecuteStep(&hasMoreData);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  if (!hasMoreData) {
    return rv;
  }

  rv = state->GetInt32(0, aCacheIdOut);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  *aFoundCacheOut = true;
  return rv;
}

// static
nsresult
DBSchema::StoragePutCache(mozIStorageConnection* aConn, Namespace aNamespace,
                          const nsAString& aKey, CacheId aCacheId)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);

  nsCOMPtr<mozIStorageStatement> state;
  nsresult rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "INSERT INTO storage (namespace, key, cache_id) VALUES(?1, ?2, ?3);"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aNamespace);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindStringParameter(1, aKey);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(2, aCacheId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->Execute();
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  return rv;
}

// static
nsresult
DBSchema::StorageForgetCache(mozIStorageConnection* aConn, Namespace aNamespace,
                             const nsAString& aKey)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);

  nsCOMPtr<mozIStorageStatement> state;
  nsresult rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "DELETE FROM storage WHERE namespace=?1 AND key=?2;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aNamespace);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindStringParameter(1, aKey);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->Execute();
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  return rv;
}

// static
nsresult
DBSchema::StorageGetKeys(mozIStorageConnection* aConn, Namespace aNamespace,
                         nsTArray<nsString>& aKeysOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);

  nsCOMPtr<mozIStorageStatement> state;
  nsresult rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT key FROM storage WHERE namespace=?1 ORDER BY rowid;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aNamespace);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool hasMoreData = false;
  while (NS_SUCCEEDED(state->ExecuteStep(&hasMoreData)) && hasMoreData) {
    nsAutoString key;
    rv = state->GetString(0, key);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
    aKeysOut.AppendElement(key);
  }

  return rv;
}

// static
nsresult
DBSchema::QueryAll(mozIStorageConnection* aConn, CacheId aCacheId,
                   nsTArray<EntryId>& aEntryIdListOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);

  nsCOMPtr<mozIStorageStatement> state;
  nsresult rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT id FROM entries WHERE cache_id=?1 ORDER BY id;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aCacheId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool hasMoreData = false;
  while (NS_SUCCEEDED(state->ExecuteStep(&hasMoreData)) && hasMoreData) {
    EntryId entryId = INT32_MAX;
    rv = state->GetInt32(0, &entryId);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
    aEntryIdListOut.AppendElement(entryId);
  }

  return rv;
}

// static
nsresult
DBSchema::QueryCache(mozIStorageConnection* aConn, CacheId aCacheId,
                     const PCacheRequest& aRequest,
                     const PCacheQueryParams& aParams,
                     nsTArray<EntryId>& aEntryIdListOut,
                     uint32_t aMaxResults)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);
  MOZ_ASSERT(aMaxResults > 0);

  if (!aParams.ignoreMethod() && !aRequest.method().LowerCaseEqualsLiteral("get")
                              && !aRequest.method().LowerCaseEqualsLiteral("head"))
  {
    return NS_OK;
  }

  nsAutoCString query(
    "SELECT id, COUNT(response_headers.name) AS vary_count "
    "FROM entries "
    "LEFT OUTER JOIN response_headers ON entries.id=response_headers.entry_id "
                                    "AND response_headers.name='vary' "
    "WHERE entries.cache_id=?1 "
      "AND entries."
  );

  nsAutoString urlToMatch;
  if (aParams.ignoreSearch()) {
    urlToMatch = aRequest.urlWithoutQuery();
    query.AppendLiteral("request_url_no_query");
  } else {
    urlToMatch = aRequest.url();
    query.AppendLiteral("request_url");
  }

  if (aParams.prefixMatch()) {
    query.AppendLiteral(" LIKE ?2 ESCAPE '\\'");
  } else {
    query.AppendLiteral("=?2");
  }

  query.AppendLiteral(" GROUP BY entries.id ORDER BY entries.id;");

  nsCOMPtr<mozIStorageStatement> state;
  nsresult rv = aConn->CreateStatement(query, getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aCacheId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  if (aParams.prefixMatch()) {
    nsAutoString escapedUrlToMatch;
    rv = state->EscapeStringForLIKE(urlToMatch, '\\', escapedUrlToMatch);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
    urlToMatch = escapedUrlToMatch;
    urlToMatch.AppendLiteral("%");
  }

  rv = state->BindStringParameter(1, urlToMatch);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool hasMoreData = false;
  while (NS_SUCCEEDED(state->ExecuteStep(&hasMoreData)) && hasMoreData) {
    // no invalid EntryId, init to least likely real value
    EntryId entryId = INT32_MAX;
    rv = state->GetInt32(0, &entryId);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    int32_t varyCount;
    rv = state->GetInt32(1, &varyCount);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    if (!aParams.ignoreVary() && varyCount > 0) {
      bool matchedByVary = false;
      rv = MatchByVaryHeader(aConn, aRequest, entryId, &matchedByVary);
      if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
      if (!matchedByVary) {
        continue;
      }
    }

    aEntryIdListOut.AppendElement(entryId);

    if (aEntryIdListOut.Length() == aMaxResults) {
      return NS_OK;
    }
  }

  return rv;
}

// static
nsresult
DBSchema::MatchByVaryHeader(mozIStorageConnection* aConn,
                            const PCacheRequest& aRequest,
                            EntryId entryId, bool* aSuccessOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);

  *aSuccessOut = false;

  nsCOMPtr<mozIStorageStatement> state;
  nsresult rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT value FROM response_headers "
    "WHERE name='vary' AND entry_id=?1;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, entryId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  nsAutoTArray<nsCString, 8> varyValues;

  bool hasMoreData = false;
  while (NS_SUCCEEDED(state->ExecuteStep(&hasMoreData)) && hasMoreData) {
    nsAutoCString value;
    rv = state->GetUTF8String(0, value);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
    varyValues.AppendElement(value);
  }
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  // Should not have called this function if this was not the case
  MOZ_ASSERT(!varyValues.IsEmpty());

  state->Reset();
  rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT name, value FROM request_headers "
    "WHERE entry_id=?1;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, entryId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  nsRefPtr<InternalHeaders> cachedHeaders = new InternalHeaders(HeadersGuardEnum::None);

  ErrorResult errorResult;

  while (NS_SUCCEEDED(state->ExecuteStep(&hasMoreData)) && hasMoreData) {
    nsAutoCString name;
    nsAutoCString value;
    rv = state->GetUTF8String(0, name);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
    rv = state->GetUTF8String(1, value);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    cachedHeaders->Append(name, value, errorResult);
    if (errorResult.Failed()) { return errorResult.ErrorCode(); };
  }
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  nsRefPtr<InternalHeaders> queryHeaders = new InternalHeaders(aRequest.headers());

  // Assume the vary headers match until we find a conflict
  bool varyHeadersMatch = true;

  for (uint32_t i = 0; i < varyValues.Length(); ++i) {
    if (varyValues[i].EqualsLiteral("*")) {
      continue;
    }

    nsAutoCString queryValue;
    queryHeaders->Get(varyValues[i], queryValue, errorResult);
    if (errorResult.Failed()) { return errorResult.ErrorCode(); };

    nsAutoCString cachedValue;
    cachedHeaders->Get(varyValues[i], cachedValue, errorResult);
    if (errorResult.Failed()) { return errorResult.ErrorCode(); };

    if (queryValue != cachedValue) {
      varyHeadersMatch = false;
      break;
    }
  }

  *aSuccessOut = varyHeadersMatch;
  return rv;
}

// static
nsresult
DBSchema::DeleteEntries(mozIStorageConnection* aConn,
                        const nsTArray<EntryId>& aEntryIdList,
                        nsTArray<nsID>& aDeletedBodyIdListOut,
                        uint32_t aPos, int32_t aLen)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);

  if (aEntryIdList.IsEmpty()) {
    return NS_OK;
  }

  MOZ_ASSERT(aPos < aEntryIdList.Length());

  if (aLen < 0) {
    aLen = aEntryIdList.Length() - aPos;
  }

  // Sqlite limits the number of entries allowed for an IN clause,
  // so split up larger operations.
  if (aLen > kMaxEntriesPerStatement) {
    uint32_t curPos = aPos;
    int32_t remaining = aLen;
    while (remaining > 0) {
      int32_t max = kMaxEntriesPerStatement;
      int32_t curLen = std::min(max, remaining);
      nsresult rv = DeleteEntries(aConn, aEntryIdList, aDeletedBodyIdListOut,
                                  curPos, curLen);
      if (NS_FAILED(rv)) { return rv; }

      curPos += curLen;
      remaining -= curLen;
    }
    return NS_OK;
  }

  nsCOMPtr<mozIStorageStatement> state;
  nsAutoCString query(
    "SELECT request_body_id, response_body_id FROM entries WHERE id IN ("
  );
  AppendListParamsToQuery(query, aEntryIdList, aPos, aLen);
  query.AppendLiteral(")");

  nsresult rv = aConn->CreateStatement(query, getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = BindListParamsToQuery(state, aEntryIdList, aPos, aLen);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool hasMoreData = false;
  while (NS_SUCCEEDED(state->ExecuteStep(&hasMoreData)) && hasMoreData) {
    // extract 0 to 2 nsID structs per row
    for (uint32_t i = 0; i < 2; ++i) {
      bool isNull = false;

      rv = state->GetIsNull(i, &isNull);
      if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

      if (!isNull) {
        nsID id;
        rv = ExtractId(state, i, &id);
        if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
        aDeletedBodyIdListOut.AppendElement(id);
      }
    }
  }

  // Dependent records removed via ON DELETE CASCADE

  query = NS_LITERAL_CSTRING(
    "DELETE FROM entries WHERE id IN ("
  );
  AppendListParamsToQuery(query, aEntryIdList, aPos, aLen);
  query.AppendLiteral(")");

  rv = aConn->CreateStatement(query, getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = BindListParamsToQuery(state, aEntryIdList, aPos, aLen);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->Execute();
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  return rv;
}

// static
nsresult
DBSchema::InsertEntry(mozIStorageConnection* aConn, CacheId aCacheId,
                      const PCacheRequest& aRequest,
                      const nsID* aRequestBodyId,
                      const PCacheResponse& aResponse,
                      const nsID* aResponseBodyId)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);

  nsCOMPtr<mozIStorageStatement> state;
  nsresult rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "INSERT INTO entries ("
      "request_method, "
      "request_url, "
      "request_url_no_query, "
      "request_referrer, "
      "request_headers_guard, "
      "request_mode, "
      "request_credentials, "
      "request_body_id, "
      "response_type, "
      "response_url, "
      "response_status, "
      "response_status_text, "
      "response_headers_guard, "
      "response_body_id, "
      "response_security_info, "
      "cache_id "
    ") VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16)"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindUTF8StringParameter(0, aRequest.method());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindStringParameter(1, aRequest.url());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindStringParameter(2, aRequest.urlWithoutQuery());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindStringParameter(3, aRequest.referrer());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(4,
    static_cast<int32_t>(aRequest.headersGuard()));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(5, static_cast<int32_t>(aRequest.mode()));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(6,
    static_cast<int32_t>(aRequest.credentials()));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = BindId(state, 7, aRequestBodyId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(8, static_cast<int32_t>(aResponse.type()));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindStringParameter(9, aResponse.url());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(10, aResponse.status());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindUTF8StringParameter(11, aResponse.statusText());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(12,
    static_cast<int32_t>(aResponse.headersGuard()));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = BindId(state, 13, aResponseBodyId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindBlobParameter(14, reinterpret_cast<const uint8_t*>
                                  (aResponse.securityInfo().get()),
                                aResponse.securityInfo().Length());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(15, aCacheId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->Execute();
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT last_insert_rowid()"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool hasMoreData = false;
  rv = state->ExecuteStep(&hasMoreData);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  int32_t entryId;
  rv = state->GetInt32(0, &entryId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "INSERT INTO request_headers ("
      "name, "
      "value, "
      "entry_id "
    ") VALUES (?1, ?2, ?3)"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  const nsTArray<PHeadersEntry>& requestHeaders = aRequest.headers();
  for (uint32_t i = 0; i < requestHeaders.Length(); ++i) {
    rv = state->BindUTF8StringParameter(0, requestHeaders[i].name());
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = state->BindUTF8StringParameter(1, requestHeaders[i].value());
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = state->BindInt32Parameter(2, entryId);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = state->Execute();
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  }

  rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "INSERT INTO response_headers ("
      "name, "
      "value, "
      "entry_id "
    ") VALUES (?1, ?2, ?3)"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  const nsTArray<PHeadersEntry>& responseHeaders = aResponse.headers();
  for (uint32_t i = 0; i < responseHeaders.Length(); ++i) {
    rv = state->BindUTF8StringParameter(0, responseHeaders[i].name());
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = state->BindUTF8StringParameter(1, responseHeaders[i].value());
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = state->BindInt32Parameter(2, entryId);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = state->Execute();
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  }

  return rv;
}

// static
nsresult
DBSchema::ReadResponse(mozIStorageConnection* aConn, EntryId aEntryId,
                       SavedResponse* aSavedResponseOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);
  MOZ_ASSERT(aSavedResponseOut);

  nsCOMPtr<mozIStorageStatement> state;
  nsresult rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT "
      "response_type, "
      "response_url, "
      "response_status, "
      "response_status_text, "
      "response_headers_guard, "
      "response_body_id, "
      "response_security_info "
    "FROM entries "
    "WHERE id=?1;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aEntryId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool hasMoreData = false;
  rv = state->ExecuteStep(&hasMoreData);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  int32_t type;
  rv = state->GetInt32(0, &type);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  aSavedResponseOut->mValue.type() = static_cast<ResponseType>(type);

  rv = state->GetString(1, aSavedResponseOut->mValue.url());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  int32_t status;
  rv = state->GetInt32(2, &status);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  aSavedResponseOut->mValue.status() = status;

  rv = state->GetUTF8String(3, aSavedResponseOut->mValue.statusText());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  int32_t guard;
  rv = state->GetInt32(4, &guard);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  aSavedResponseOut->mValue.headersGuard() =
    static_cast<HeadersGuardEnum>(guard);

  bool nullBody = false;
  rv = state->GetIsNull(5, &nullBody);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  aSavedResponseOut->mHasBodyId = !nullBody;

  if (aSavedResponseOut->mHasBodyId) {
    rv = ExtractId(state, 5, &aSavedResponseOut->mBodyId);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  }

  uint8_t* data = nullptr;
  uint32_t dataLength = 0;
  rv = state->GetBlob(6, &dataLength, &data);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  aSavedResponseOut->mValue.securityInfo().Adopt(
    reinterpret_cast<char*>(data), dataLength);

  rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT "
      "name, "
      "value "
    "FROM response_headers "
    "WHERE entry_id=?1;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aEntryId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  while (NS_SUCCEEDED(state->ExecuteStep(&hasMoreData)) && hasMoreData) {
    PHeadersEntry header;

    rv = state->GetUTF8String(0, header.name());
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = state->GetUTF8String(1, header.value());
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    aSavedResponseOut->mValue.headers().AppendElement(header);
  }

  return rv;
}

// static
nsresult
DBSchema::ReadRequest(mozIStorageConnection* aConn, EntryId aEntryId,
                      SavedRequest* aSavedRequestOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConn);
  MOZ_ASSERT(aSavedRequestOut);

  nsCOMPtr<mozIStorageStatement> state;
  nsresult rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT "
      "request_method, "
      "request_url, "
      "request_url_no_query, "
      "request_referrer, "
      "request_headers_guard, "
      "request_mode, "
      "request_credentials, "
      "request_body_id "
    "FROM entries "
    "WHERE id=?1;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aEntryId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool hasMoreData = false;
  rv = state->ExecuteStep(&hasMoreData);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->GetUTF8String(0, aSavedRequestOut->mValue.method());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->GetString(1, aSavedRequestOut->mValue.url());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->GetString(2, aSavedRequestOut->mValue.urlWithoutQuery());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->GetString(3, aSavedRequestOut->mValue.referrer());
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  int32_t guard;
  rv = state->GetInt32(4, &guard);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  aSavedRequestOut->mValue.headersGuard() =
    static_cast<HeadersGuardEnum>(guard);

  int32_t mode;
  rv = state->GetInt32(5, &mode);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  aSavedRequestOut->mValue.mode() = static_cast<RequestMode>(mode);

  int32_t credentials;
  rv = state->GetInt32(6, &credentials);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  aSavedRequestOut->mValue.credentials() =
    static_cast<RequestCredentials>(credentials);

  bool nullBody = false;
  rv = state->GetIsNull(7, &nullBody);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  aSavedRequestOut->mHasBodyId = !nullBody;

  if (aSavedRequestOut->mHasBodyId) {
    rv = ExtractId(state, 7, &aSavedRequestOut->mBodyId);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
  }

  rv = aConn->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT "
      "name, "
      "value "
    "FROM request_headers "
    "WHERE entry_id=?1;"
  ), getter_AddRefs(state));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  rv = state->BindInt32Parameter(0, aEntryId);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  while (NS_SUCCEEDED(state->ExecuteStep(&hasMoreData)) && hasMoreData) {
    PHeadersEntry header;

    rv = state->GetUTF8String(0, header.name());
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = state->GetUTF8String(1, header.value());
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    aSavedRequestOut->mValue.headers().AppendElement(header);
  }

  return rv;
}

// static
void
DBSchema::AppendListParamsToQuery(nsACString& aQuery,
                                  const nsTArray<EntryId>& aEntryIdList,
                                  uint32_t aPos, int32_t aLen)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT((aPos + aLen) <= aEntryIdList.Length());
  for (int32_t i = aPos; i < aLen; ++i) {
    if (i == 0) {
      aQuery.AppendLiteral("?");
    } else {
      aQuery.AppendLiteral(",?");
    }
  }
}

// static
nsresult
DBSchema::BindListParamsToQuery(mozIStorageStatement* aState,
                                const nsTArray<EntryId>& aEntryIdList,
                                uint32_t aPos, int32_t aLen)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT((aPos + aLen) <= aEntryIdList.Length());
  for (int32_t i = aPos; i < aLen; ++i) {
    nsresult rv = aState->BindInt32Parameter(i, aEntryIdList[i]);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

// static
nsresult
DBSchema::BindId(mozIStorageStatement* aState, uint32_t aPos, const nsID* aId)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aState);
  nsresult rv;

  if (!aId) {
    rv = aState->BindNullParameter(aPos);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
    return rv;
  }

  char idBuf[NSID_LENGTH];
  aId->ToProvidedString(idBuf);
  rv = aState->BindUTF8StringParameter(aPos, nsAutoCString(idBuf));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  return rv;
}

// static
nsresult
DBSchema::ExtractId(mozIStorageStatement* aState, uint32_t aPos, nsID* aIdOut)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aState);
  MOZ_ASSERT(aIdOut);

  nsAutoCString idString;
  nsresult rv = aState->GetUTF8String(aPos, idString);
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool success = aIdOut->Parse(idString.get());
  if (NS_WARN_IF(!success)) { return NS_ERROR_UNEXPECTED; }

  return rv;
}

} // namespace cache
} // namespace dom
} // namespace mozilla
