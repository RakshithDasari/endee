#include <string>
#include "ndd.hpp"


bool IndexManager::newcreateIndex(std::string& index_name, UserType user_type,
                                    std::vector<struct NewIndexConfig> dense_indexes,
                                    std::vector<struct SparseIndexConfig> sparse_indexes)
{
    for(int i=0; i<dense_indexes.size(); i++){
        printf("%s: name:%s, M:%zu\n", __func__, dense_indexes[i].sub_index_name.c_str(), dense_indexes[i].M);
    }

    for(int i=0; i<sparse_indexes.size(); i++){
        printf("%s: name:%s, sparse_dim:%zu\n", __func__, sparse_indexes[i].sub_index_name.c_str(), sparse_indexes[i].sparse_dim);
    }
    return true;
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
