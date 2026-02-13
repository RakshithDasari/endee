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
    std::unordered_map<std::string, std::shared_ptr<SubDenseCacheEntry>> dense_map;
    std::unordered_map<std::string, std::shared_ptr<SubSparseCacheEntry>> sparse_map;
    std::shared_ptr<SubDenseCacheEntry> dense_sub_index_cache;
    std::shared_ptr<SubSparseCacheEntry> sparse_sub_index_cache;

    // Cleanup guard — removes partial artifacts if we don't reach commit
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
            throw std::runtime_error("Index with this name already exists for this user");
        }
    }
    // check if it exists in the filesystem
    index_path = data_dir_ + "/" + index_id;
    if(std::filesystem::exists(index_path)) {
        // throw std::runtime_error("Index with this name already exists for this user");
        ret.first = false;
        ret.second = "index_name: " + index_name + " already exists.";
        goto exit_newcreateIndex;
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
        std::string lmdb_dir = data_dir_ + "/" + index_id + "/ids";
        auto id_mapper = std::make_shared<IDMapper>(lmdb_dir, true, user_type);
        std::filesystem::create_directory(data_dir_ + "/" + index_id + "/vectors");

        for(int i=0; i< dense_indexes.size(); i++){
            auto& dense_sub_index = dense_indexes[i];
            dense_sub_index_cache = std::make_shared<SubDenseCacheEntry>();

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

            std::string vector_storage_dir = data_dir_ + "/" + index_id + "/vectors" + "/vectors_" + dense_sub_index.sub_index_name;
            if(!std::filesystem::create_directory(vector_storage_dir)){
                if (std::filesystem::exists(vector_storage_dir)) {
                    throw std::runtime_error("Duplicate named sub index: " + dense_sub_index.sub_index_name);
                }else{
                    throw std::runtime_error("Error: while creating Folder" + vector_storage_dir);
                }
            }

            /**
             * Check if there is a duplicate sub index from the filesystem.
             */

            // dense_sub_index_cache->vector_storage = std::make_shared<VectorStorage>(vector_storage_dir,
            //                                                                 dense_sub_index.dim,
            //                                                                 dense_sub_index.quant_level);

        }
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
