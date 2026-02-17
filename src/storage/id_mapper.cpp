#include <string>
#include "id_mapper.hpp"

template <bool use_deleted_ids>
bool IDMapper::newcreate_ids_batch(std::vector<ndd::GenericVectorObject>& vectors, void* wal_ptr)
{
    bool ret = false;
    constexpr idInt INVALID_LABEL = static_cast<idInt>(-1);

    if(vectors.empty()){
        return ret;
    }

    LOG_DEBUG("=== create_ids_batch START ===");

    std::vector<std::tuple<std::string, idInt, bool, bool>> id_tuples;

    id_tuples.reserve(vectors.size());
    for(const auto& vec : vectors) {
        // true means that the ID is new and false means that the ID already exists
        // is_reused defaults to false
        id_tuples.emplace_back(vec.id, INVALID_LABEL, true, false);
    }

    LOG_DEBUG("--- STEP 2: LMDB database check ---");
    {
        MDBX_txn* txn;
        int rc = mdbx_txn_begin(env_, nullptr, MDBX_TXN_RDONLY, &txn);
        if(rc != MDBX_SUCCESS) {
            LOG_DEBUG("ERROR: Failed to begin read-only transaction: " << mdbx_strerror(rc));
            throw std::runtime_error("Failed to begin read-only transaction: "
                                        + std::string(mdbx_strerror(rc)));
        }
        LOG_DEBUG("LMDB read-only transaction started successfully");

        try {
            int keys_checked = 0;
            for(auto& tup : id_tuples) {
                if(std::get<1>(tup) == INVALID_LABEL) {
                    const std::string& str_id = std::get<0>(tup);
                    MDBX_val key{(void*)str_id.c_str(), str_id.size()};
                    MDBX_val data;

                    // Add debug logging
                    LOG_DEBUG("LMDB: Checking key[" << keys_checked << "]: [" << str_id
                                                    << "] size: " << str_id.size());
                    keys_checked++;

                    rc = mdbx_get(txn, dbi_, &key, &data);
                    if(rc == MDBX_SUCCESS) {
                        idInt existing_id = *(idInt*)data.iov_base;
                        LOG_DEBUG("LMDB: ✓ FOUND existing ID: " << existing_id << " for key: ["
                                                                << str_id << "]");
                        std::get<1>(tup) = existing_id;
                        std::get<2>(tup) = false;  // ID already exists
                    } else if(rc == MDBX_NOTFOUND) {
                        LOG_DEBUG("LMDB: ✗ NOT FOUND: [" << str_id << "]");
                        std::get<1>(tup) = 0;
                    } else {
                        LOG_DEBUG("LMDB: ERROR for key: [" << str_id
                                                            << "] error: " << mdbx_strerror(rc));
                        mdbx_txn_abort(txn);
                        throw std::runtime_error("Database error checking ID: "
                                                    + std::string(mdbx_strerror(rc)));
                    }
                }
            }
            LOG_DEBUG("LMDB: Checked " << keys_checked << " keys in database");
            mdbx_txn_abort(txn);
            LOG_DEBUG("LMDB check done");
        } catch(...) {
            mdbx_txn_abort(txn);
            throw;
        }
    }

    //Count and generate new IDs
    LOG_DEBUG("--- STEP 3: Count and generate new IDs ---");
    size_t total_new_ids_needed =
            std::count_if(id_tuples.begin(), id_tuples.end(), [](const auto& t) {
                return std::get<1>(t) == 0;
            });
    LOG_DEBUG("Total new IDs needed: " << total_new_ids_needed);

    size_t fresh_ids_count = total_new_ids_needed;
    size_t deleted_index = 0;

    if(use_deleted_ids) {
        // Use deleted IDs first, but ONLY for entries that are actually new (not found in DB)
        std::vector<idInt> deletedIds = getDeletedIds(fresh_ids_count);

        for(auto& tup : id_tuples) {
            // Only assign deleted IDs to entries that are new (id=0 and is_new=true)
            if(std::get<1>(tup) == 0 && std::get<2>(tup) == true
                && deleted_index < deletedIds.size()) {
                std::get<1>(tup) = deletedIds[deleted_index++];
                std::get<3>(tup) = true;  // Mark as reused
                // Keep std::get<2>(tup) as true because this still needs to be written to DB
            }
        }
        fresh_ids_count -= deleted_index;  // Reduce by actual number of deleted IDs used
    }

    if(total_new_ids_needed > 0) {
        LOG_DEBUG("Generating " << fresh_ids_count << " fresh IDs");

        std::vector<idInt> new_ids;
        if(fresh_ids_count > 0) {
            new_ids = get_next_ids(fresh_ids_count);
        }

        // CRITICAL FIX: Log to WAL AFTER generating IDs (minimal risk window)
        if(wal_ptr) {
            WriteAheadLog* wal = static_cast<WriteAheadLog*>(wal_ptr);
            std::vector<WriteAheadLog::WALEntry> wal_entries;

            // Log reused IDs
            for(const auto& tup : id_tuples) {
                if(std::get<2>(tup) && std::get<1>(tup) != 0) {
                    wal_entries.push_back({WALOperationType::VECTOR_ADD, std::get<1>(tup)});
                }
            }

            // Log fresh IDs
            for(idInt id : new_ids) {
                wal_entries.push_back({WALOperationType::VECTOR_ADD, id});
            }

            if(!wal_entries.empty()) {
                wal->log(wal_entries);
            }
        }

        if(fresh_ids_count > 0 && new_ids.size() != fresh_ids_count) {
            throw std::runtime_error("Mismatch: get_next_ids returned "
                                        + std::to_string(new_ids.size()) + " but expected "
                                        + std::to_string(fresh_ids_count));
        }

        size_t new_id_index = 0;

        // Step 4: Write txn with auto-resize retry
        LOG_DEBUG("--- STEP 4: Writing to database ---");
        auto try_write = [&](MDBX_txn* txn) -> int {
            int writes_attempted = 0;
            for(auto& tup : id_tuples) {
                // Write entries that need to be written to DB (is_new=true) but don't have ID=0
                if(std::get<2>(tup) == true && std::get<1>(tup) != 0) {
                    const std::string& str_id = std::get<0>(tup);
                    idInt id = std::get<1>(tup);

                    MDBX_val key{(void*)str_id.c_str(), str_id.size()};
                    MDBX_val data{&id, sizeof(idInt)};

                    // Add debug logging for write operations
                    LOG_DEBUG("WRITE[" << writes_attempted << "]: key=[" << str_id
                                        << "] size=" << str_id.size() << " ID=" << id);
                    writes_attempted++;

                    int rc = mdbx_put(txn, dbi_, &key, &data, MDBX_UPSERT);
                    if(rc == MDBX_MAP_FULL) {
                        LOG_DEBUG("WRITE ERROR: MDBX_MAP_FULL for key=[" << str_id << "]");
                        return MDBX_MAP_FULL;
                    }
                    if(rc != MDBX_SUCCESS) {
                        LOG_DEBUG("WRITE ERROR: [" << str_id
                                                    << "] error: " << mdbx_strerror(rc));
                        return rc;
                    }

                    LOG_DEBUG("WRITE SUCCESS: [" << str_id << "] with ID: " << id);

                } else if(std::get<1>(tup) == 0) {
                    // Handle remaining entries that still need new IDs
                    if(new_id_index >= new_ids.size()) {
                        LOG_DEBUG("ERROR: new_id_index ("
                                    << new_id_index << ") >= new_ids.size() (" << new_ids.size()
                                    << ")");
                        return MDBX_PROBLEM;  // Internal error
                    }
                    idInt new_id = new_ids[new_id_index++];
                    const std::string& str_id = std::get<0>(tup);

                    MDBX_val key{(void*)str_id.c_str(), str_id.size()};
                    MDBX_val data{&new_id, sizeof(idInt)};

                    writes_attempted++;

                    int rc = mdbx_put(txn, dbi_, &key, &data, MDBX_UPSERT);
                    if(rc == MDBX_MAP_FULL) {
                        LOG_DEBUG("WRITE_NEW ERROR: MDBX_MAP_FULL for key=[" << str_id << "]");
                        return MDBX_MAP_FULL;
                    }
                    if(rc != MDBX_SUCCESS) {
                        LOG_DEBUG("WRITE_NEW ERROR: [" << str_id
                                                        << "] error: " << mdbx_strerror(rc));
                        return rc;
                    }

                    std::get<1>(tup) = new_id;
                }
            }
            return MDBX_SUCCESS;
        };

        MDBX_txn* txn;
        int rc = mdbx_txn_begin(env_, nullptr, MDBX_TXN_READWRITE, &txn);
        if(rc != MDBX_SUCCESS) {
            throw std::runtime_error("Failed to begin write transaction: "
                                        + std::string(mdbx_strerror(rc)));
        }

        rc = try_write(txn);
        // MDBX auto-grows, no manual resize needed
        if(rc != MDBX_SUCCESS) {
            mdbx_txn_abort(txn);
            throw std::runtime_error("Failed to insert new IDs: "
                                        + std::string(mdbx_strerror(rc)));
        }

        rc = mdbx_txn_commit(txn);
        if(rc != MDBX_SUCCESS) {
            throw std::runtime_error("Failed to commit transaction: "
                                        + std::string(mdbx_strerror(rc)));
        }
        LOG_DEBUG("Write transaction committed successfully");
    } else {
        LOG_DEBUG("No new IDs needed, skipping write transaction");
    }

    // Final state logging
    LOG_DEBUG("--- FINAL RESULTS ---");
    std::vector<std::pair<idInt, bool>> result;
    result.reserve(id_tuples.size());
    for(size_t i = 0; i < id_tuples.size(); i++) {
        const auto& tup = id_tuples[i];
        bool is_new_to_hnsw = std::get<2>(tup);
        // If the ID was reused from deleted list, treat it as an update (not new to HNSW)
        if(std::get<3>(tup)) {
            is_new_to_hnsw = false;
        }
        vectors[i].numeric_id.first = std::get<1>(tup);
        vectors[i].numeric_id.second = is_new_to_hnsw;
    }
    ret = true;

    LOG_DEBUG("=== create_ids_batch END ===");

exit_newcreate_ids_batch:
    return ret;
}

template bool IDMapper::newcreate_ids_batch<false>(
    std::vector<ndd::GenericVectorObject>&, void*);

template bool IDMapper::newcreate_ids_batch<true>(
    std::vector<ndd::GenericVectorObject>&, void*);