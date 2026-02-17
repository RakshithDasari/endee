#include <string>
#include "ndd.hpp"

template <typename Map, typename Key, typename Value>
void insert_or_throw(Map& map, Key&& key, Value&& value) {
    auto [it, inserted] = map.try_emplace(
        std::forward<Key>(key), std::forward<Value>(value));
    if (!inserted) {
        throw std::runtime_error("Duplicate key: " + it->first);
    }
}

/*TODO: Critical*/
void IndexManager::newloadIndex(const std::string& index_id){
    std::runtime_error("IndexManager::newloadIndex is not implemented");
}

std::pair<bool, std::string> IndexManager::newcreateIndex(std::string& username,
                                    UserType user_type, std::string& index_name,
                                    std::vector<struct NewIndexConfig> dense_indexes,
                                    std::vector<struct SparseIndexConfig> sparse_indexes)
{
    std::pair<bool, std::string> ret;
    ret.first = true;
    ret.second = "";

    std::filesystem::space_info space_info;
    std::error_code ec;
    bool committed = false;
    std::string index_path = "";
    std::unordered_map<std::string, std::shared_ptr<DenseCacheSubEntry>> dense_cache_map;
    std::unordered_map<std::string, std::shared_ptr<SubSparseCacheEntry>> sparse_cache_map;
    std::shared_ptr<DenseCacheSubEntry> dense_sub_index_cache;
    std::shared_ptr<SubSparseCacheEntry> sparse_sub_index_cache;

    // Cleanup guard — removes partial artifacts if we don't reach commit
    // TODO: This function is not complete
    auto cleanup = [&]() {
        if (committed)
            return;
        printf("%s: cleanup triggered\n", __func__);
        // {
        //     std::unique_lock<std::shared_mutex> lock(indices_mutex_);
        //     indices_.erase(index_id);
        //     indices_list_.remove(index_id);
        // }
        // // Remove partial directories
        std::error_code ec;
        // std::filesystem::remove_all(index_path, ec);
        // // Log ec if needed, but don't throw
    };

    /**
     * Check if index name already exists
     */
    std::string index_id =  username + "/" + index_name;
    auto existing_indices = metadata_manager_->listUserIndexes(username);
    for(const auto& existing : existing_indices) {
        if(existing.first == index_name) {
            ret.first = false;
            ret.second = "index_name: " + index_name + " already exists.";
            goto exit_newcreateIndex;
        }
    }
    // check if it exists in the filesystem
    index_path = data_dir_ + "/" + index_id;
    if(std::filesystem::exists(index_path)) {
        throw std::runtime_error("index_name: " + index_name + " already exists.");
    }

    // Check if there is enough space on the disk
    space_info = std::filesystem::space(data_dir_, ec);
    // std::cout << "space available: " << space_info.available/GB << "GB \n";
    if (!ec && space_info.available < settings::MINIMUM_REQUIRED_FS_BYTES) {
        throw std::runtime_error("Insufficient disk space to create index");
    }

    // Check if there exist any sub indexes
    if(dense_indexes.size() == 0 && sparse_indexes.size() == 0){
        throw std::runtime_error("No dense or sparse indexes passed");
    }

    // LOG_INFO("Creating IDMapper for index "
    //             << index_id << " with user type: " << userTypeToString(user_type));

    try{
        std::string lmdb_dir = index_path + "/ids";
        std::string vec_data_dir = index_path + "/vectors";

        /**
         * TODO: add error handing while creating directories here.
         * check duplicate creates for the same name.
         */

        std::filesystem::create_directory(index_path);
        std::filesystem::create_directory(vec_data_dir);

        auto id_mapper = std::make_shared<IDMapper>(lmdb_dir, true, user_type);

        for(int i=0; i< dense_indexes.size(); i++){
            auto& dense_sub_index = dense_indexes[i];
            dense_sub_index_cache = std::make_shared<DenseCacheSubEntry>();

            /**
             * Check limits for this user's type
             */
            if(dense_sub_index.size_in_millions > getMaxVectorsPerIndex(user_type)){
                ret.first = false;
                ret.second = "Size in millions is greater than max allowed : " + std::to_string(dense_sub_index.size_in_millions);
                goto exit_newcreateIndex_cleanup;
            }

            std::cout << "space type: " << dense_sub_index.space_type_str << "\n";
            hnswlib::SpaceType space_type = hnswlib::getSpaceType(dense_sub_index.space_type_str);

            dense_sub_index_cache->vector_store = std::make_shared<VectorStore>(
                                vec_data_dir + "/vectors_" + dense_sub_index.sub_index_name,
                                dense_sub_index.dim, dense_sub_index.quant_level);


            dense_sub_index_cache->alg = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                                                            dense_sub_index.max_elements,
                                                            space_type,
                                                            dense_sub_index.dim,
                                                            dense_sub_index.M,
                                                            dense_sub_index.ef_construction,
                                                            settings::RANDOM_SEED,
                                                            dense_sub_index.quant_level,
                                                            dense_sub_index.checksum);

            dense_sub_index_cache->alg->setVectorFetcher([vs = dense_sub_index_cache->vector_store]
                    (ndd::idInt label, uint8_t* buffer) {
                        return vs->get_vector_bytes(label, buffer);
                    }
            );

            /* add this dense_sub_index_cache entry to dense_map*/
            auto[it, inserted] = dense_cache_map.insert({dense_sub_index.sub_index_name, std::move(dense_sub_index_cache)});
            if(!inserted){
                LOG_ERROR("Duplicate sub index name: " + dense_sub_index.sub_index_name);
                ret.first = false;
                ret.second = "duplicate sub index_name: " + dense_sub_index.sub_index_name;
                goto exit_newcreateIndex_cleanup;
            }
        }

        /**
         * TODO: Do a for loop for all sparse vectors
         */
        // for(int i=0; i< sparse_indexes.size(); i++){
        // }


        //add NewCacheEntry against index name to IndexManager.newindices_
        {
            auto cache_entry = NewCacheEntry::create(index_path, index_id, id_mapper,
                                                                    std::move(dense_cache_map),
                                                                    std::move(sparse_cache_map),
                                                                    std::chrono::system_clock::now());
            if(!cache_entry){
                ret.first = false;
                ret.second = "unable to allocate NewCacheEntry";
                goto exit_newcreateIndex_cleanup;
            }

            std::unique_lock<std::shared_mutex> lock(indices_mutex_);

            auto[it, inserted] = newindices_.emplace(index_id, std::move(cache_entry));
            if(!inserted){
                ret.first = false;
                ret.second = "found a duplicate entry in newindices_";
                goto exit_newcreateIndex_cleanup;
            }
            it->second->markUpdated();
            indices_list_.push_front(index_id);
        }

        /*
        // TESTING CODE ONLY: Print dense sub-indexes
        for (const auto& [idx_id, entry] : newindices_) {
            LOG_INFO("Index: " << entry->index_id);

            // Print dense sub-indexes
            LOG_INFO("  Dense sub-indexes (" << entry->dense_vectors.size() << "):");
            for (const auto& [sub_name, dense_entry] : entry->dense_vectors) {
                LOG_INFO("   subvec_name: [" << sub_name << "]"
                    << " dim=" << dense_entry->alg->getDimension()
                    << " max_elements=" << dense_entry->alg->getMaxElements()
                    << " elements=" << dense_entry->alg->getElementsCount()
                    << " M=" << dense_entry->alg->getM()
                    << " ef_construction=" << dense_entry->alg->getEfConstruction()
                    << " space=" << dense_entry->alg->getSpaceTypeStr()
                    << " quant=" << static_cast<int>(dense_entry->alg->getQuantLevel())
                    << " checksum=" << dense_entry->alg->getChecksum()
                    << " remaining_capacity=" << dense_entry->alg->getRemainingCapacity()
                );
            }
        }
        */

        goto exit_newcreateIndex;
    } catch (...){
        cleanup();
        throw;
    }

exit_newcreateIndex_cleanup:
cleanup();

exit_newcreateIndex:
    return ret;
}


/**
 * new impl. of getIndexEntry. Copies the logic as is
 */
std::shared_ptr<NewCacheEntry> IndexManager::newgetIndexEntry(std::string &index_id){

    /*First try with reader's lock*/
    {
        //std::shared_lock<std::shared_mutex> read_lock(indices_mutex_);
        auto it = newindices_.find(index_id);
        if(it != newindices_.end()) {
            std::cout << __func__ << " it not end() for index_id: " << index_id << std::endl;
            return it->second;
        }
    }

    /*Try with writer's lock*/
    {
        /**
         * XXX: incomplete IMPL. phase. This code snippet should not be called right now.
         * This is because all the required functions are not implemented.
         */
        return nullptr;

        std::unique_lock<std::shared_mutex> write_lock(indices_mutex_);
            auto it = newindices_.find(index_id);
            if(it == newindices_.end()) {
                newloadIndex(index_id);  // modifies indices_ [NOT IMPLEMENTED]
                evictIfNeeded();      // Clean eviction only
            }
            it = newindices_.find(index_id);
            if(it == newindices_.end()) {
                return nullptr;
                // throw std::runtime_error("[ERROR] Failed to load index");
            }
            return it->second;
    }
}

/**
 * Adds list of named vectors.
 */
std::pair<bool, std::string> IndexManager::addNamedVectors(std::string& index_id,
                                    std::vector<ndd::GenericVectorObject>& vectors)
{
    std::pair<bool, std::string> ret;
    std::shared_ptr<NewCacheEntry> entry = nullptr;
    ret.first = true;
    ret.second = "";

    if(vectors.empty()) {
        ret.first = false;
        ret.second = "no vectors to add";
        LOG_ERROR(ret.second);
        goto exit_addNamedVectors;
    }

#if 0
    std::cout << "index_id: " << index_id << std::endl;

    // Debug: print all inserted vectors
    for (const auto& gvo : vectors) {
        std::cout << "=== Vector ID: " << gvo.id << " ===" << std::endl;
        std::cout << "  Filter: " << gvo.filter << std::endl;
        std::cout << "  Meta: " << std::string(gvo.meta.begin(), gvo.meta.end()) << std::endl;

        for (const auto& [name, dvo] : gvo.dense_vectors) {
            std::cout << "  Dense [" << name << "] norm=" << dvo.norm << " vector=[";
            for (size_t i = 0; i < dvo.vector.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << dvo.vector[i];
            }
            std::cout << "]" << std::endl;
        }

        for (const auto& [name, svo] : gvo.sparse_vectors) {
            std::cout << "  Sparse [" << name << "] indices=[";
            for (size_t i = 0; i < svo.sparse_ids.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << svo.sparse_ids[i] << ":" << svo.sparse_values[i];
            }
            std::cout << "]" << std::endl;
        }
    }
    std::cout << "Total vectors inserted: " << vectors.size() << std::endl;

#endif //if 0

    /* Get index from index_id*/
    entry = newgetIndexEntry(index_id);
    if(!entry){
        ret.first = false;
        ret.second = "Could not find index: " + index_id;

        /*For now*/
        ret.second += " XXXX: MAYBE THIS IS BECAUSE the IMPLEMENTATION OF newgetIndexEntry is incomplete";
        LOG_INFO(ret.second);
        goto exit_addNamedVectors;
    }

    std::cout << "entry.index_id - " << entry->index_id << std::endl;
    /* Create intID for each StringID using IDMapper*/



    /**
     * TODO: Critical
     * Skipping usage of WAL right now.
     * We have to decide if WAL needs to be a per sub-index concept or not
     */



exit_addNamedVectors:
    return ret;
}


/**
 * returns <true, ""> if index config is sane
 */
std::pair<bool, std::string> check_index_config_sanity(struct NewIndexConfig index_config){
    std::pair<bool, std::string> ret;
    ret.first = true;
    ret.second = "";

    if(index_config.dim < settings::MIN_DIMENSION || index_config.dim > settings::MAX_DIMENSION) {
        ret.first = false;
        ret.second += "Invalid dimension: " + std::to_string(index_config.dim)
                    + ". Should be between " + std::to_string(settings::MIN_DIMENSION)
                    + " and " + std::to_string(settings::MAX_DIMENSION);
        LOG_ERROR(ret.second);
        return ret;
    }

    if(index_config.M < settings::MIN_M || index_config.M > settings::MAX_M) {
        ret.first = false;
        ret.second += "Invalid M: " + std::to_string(index_config.M)
                    + ". Should be between " + std::to_string(settings::MIN_M)
                    + " and " + std::to_string(settings::MAX_M);
        LOG_ERROR(ret.second);
        return ret;
    }

    if(index_config.ef_construction < settings::MIN_EF_CONSTRUCT ||
        index_config.ef_construction > settings::MAX_EF_CONSTRUCT)
    {
        ret.first = false;
        ret.second += "Invalid ef_con: " + std::to_string(index_config.ef_construction)
                    + ". Should be between " + std::to_string(settings::MIN_EF_CONSTRUCT)
                    + " and " + std::to_string(settings::MAX_EF_CONSTRUCT);
        LOG_ERROR(ret.second);
        return ret;
    }

    if(index_config.quant_level == ndd::quant::QuantizationLevel::UNKNOWN){
        ret.first = false;
        ret.second += "Invalid precision";
        LOG_ERROR(ret.second);
        return ret;
    }

    /**
     * TODO: Check its need and update this as required
     */
    // if(index_config.size_in_millions == 0 ||
    //     index_config.size_in_millions > settings::MAX_SIZE_IN_MILLIONS)
    // {
    //     ret.first = false;
    //     ret.second += "Invalid size_in_millions: " + std::to_string(index_config.size_in_millions)
    //         + ". Should be > 0 and < " + std::to_string(settings::MAX_SIZE_IN_MILLIONS);
    //     LOG_ERROR(ret.second);
    //     return ret;
    // }

    /**
     * TODO: Check the following:
     * sub_index_name needs to be sane.
     * sparse_dim needs to be of a certain max dimension.
     * space type needs to be checked to a certain strings only
     * what is the difference max_elements and size_in_millions ? 
     */

    return ret;
}

/**
 * Check if this is okay for validating index name
std::pair<bool, std::string> validate_index_name(const std::string& name) {
    // Not empty
    if (name.empty()) {
        return {false, "Index name cannot be empty"};
    }

    // Length limit
    if (name.size() > 128) {
        return {false, "Index name too long (max 128 characters)"};
    }

    // Only allow alphanumeric, hyphens, underscores
    for (char c : name) {
        if (!std::isalnum(c) && c != '-' && c != '_') {
            return {false, "Index name contains invalid character: '" + std::string(1, c) + "'. Only alphanumeric, hyphens, and underscores allowed"};
        }
    }

    // Don't allow starting with a hyphen or underscore
    if (name[0] == '-' || name[0] == '_') {
        return {false, "Index name must start with an alphanumeric character"};
    }

    // Block path traversal attempts
    if (name.find("..") != std::string::npos || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        return {false, "Index name contains illegal sequence"};
    }

    return {true, ""};
}
*/
