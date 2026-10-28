#include "basler_driver/genicam.hpp"

#include <cmath>
#include <functional>

namespace basler_driver {

void throwIfError(GError* error) {
    if (error != nullptr) {
        std::string message = error->message;
        g_error_free(error);
        throw GenicamException(message);
    }
}

Genicam::Genicam(ArvDevice* device) : device_(device) {}

ArvGcNode* Genicam::node(const std::string& name) const {
    ArvGcNode* gc_node = arv_device_get_feature(device_, name.c_str());
    if (gc_node == nullptr || !ARV_IS_GC_FEATURE_NODE(gc_node)) {
        throw GenicamException("Node '" + name + "' does not exist.");
    }
    return gc_node;
}

void Genicam::requireWritable(const std::string& name) const {
    node(name);
    if (!isAvailable(name)) {
        throw GenicamException("Node '" + name + "' is not available.");
    }
    if (!isWritable(name)) {
        throw GenicamException("Node is not writable.");
    }
}

bool Genicam::isAvailable(const std::string& name) const {
    ArvGcNode* gc_node = arv_device_get_feature(device_, name.c_str());
    if (gc_node == nullptr || !ARV_IS_GC_FEATURE_NODE(gc_node)) {
        return false;
    }
    GError* error = nullptr;
    const bool implemented = arv_gc_feature_node_is_implemented(ARV_GC_FEATURE_NODE(gc_node), &error);
    if (error != nullptr) {
        g_error_free(error);
        return false;
    }
    const bool available = implemented && arv_gc_feature_node_is_available(ARV_GC_FEATURE_NODE(gc_node), &error);
    if (error != nullptr) {
        g_error_free(error);
        return false;
    }
    return available;
}

bool Genicam::isReadable(const std::string& name) const {
    if (!isAvailable(name)) {
        return false;
    }
    const ArvGcAccessMode mode = arv_gc_feature_node_get_actual_access_mode(ARV_GC_FEATURE_NODE(node(name)));
    return mode == ARV_GC_ACCESS_MODE_RO || mode == ARV_GC_ACCESS_MODE_RW;
}

bool Genicam::isWritable(const std::string& name) const {
    if (!isAvailable(name)) {
        return false;
    }
    ArvGcFeatureNode* feature = ARV_GC_FEATURE_NODE(node(name));
    const ArvGcAccessMode mode = arv_gc_feature_node_get_actual_access_mode(feature);
    if (mode != ARV_GC_ACCESS_MODE_WO && mode != ARV_GC_ACCESS_MODE_RW) {
        return false;
    }
    GError* error = nullptr;
    const bool locked = arv_gc_feature_node_is_locked(feature, &error);
    if (error != nullptr) {
        g_error_free(error);
        return false;
    }
    return !locked;
}

double Genicam::getFloat(const std::string& name) const {
    ArvGcNode* gc_node = node(name);
    GError* error = nullptr;
    double value = 0.0;
    if (ARV_IS_GC_FLOAT(gc_node)) {
        value = arv_gc_float_get_value(ARV_GC_FLOAT(gc_node), &error);
    } else if (ARV_IS_GC_INTEGER(gc_node)) {
        value = static_cast<double>(arv_gc_integer_get_value(ARV_GC_INTEGER(gc_node), &error));
    } else {
        throw GenicamException("Node '" + name + "' is not numeric.");
    }
    throwIfError(error);
    return value;
}

void Genicam::setFloat(const std::string& name, double value) const {
    requireWritable(name);
    ArvGcNode* gc_node = node(name);
    GError* error = nullptr;
    if (ARV_IS_GC_FLOAT(gc_node)) {
        arv_gc_float_set_value(ARV_GC_FLOAT(gc_node), value, &error);
    } else if (ARV_IS_GC_INTEGER(gc_node)) {
        arv_gc_integer_set_value(ARV_GC_INTEGER(gc_node), static_cast<int64_t>(std::lround(value)), &error);
    } else {
        throw GenicamException("Node '" + name + "' is not numeric.");
    }
    throwIfError(error);
}

double Genicam::getFloatMin(const std::string& name) const {
    ArvGcNode* gc_node = node(name);
    GError* error = nullptr;
    double value = 0.0;
    if (ARV_IS_GC_FLOAT(gc_node)) {
        value = arv_gc_float_get_min(ARV_GC_FLOAT(gc_node), &error);
    } else if (ARV_IS_GC_INTEGER(gc_node)) {
        value = static_cast<double>(arv_gc_integer_get_min(ARV_GC_INTEGER(gc_node), &error));
    } else {
        throw GenicamException("Node '" + name + "' is not numeric.");
    }
    throwIfError(error);
    return value;
}

double Genicam::getFloatMax(const std::string& name) const {
    ArvGcNode* gc_node = node(name);
    GError* error = nullptr;
    double value = 0.0;
    if (ARV_IS_GC_FLOAT(gc_node)) {
        value = arv_gc_float_get_max(ARV_GC_FLOAT(gc_node), &error);
    } else if (ARV_IS_GC_INTEGER(gc_node)) {
        value = static_cast<double>(arv_gc_integer_get_max(ARV_GC_INTEGER(gc_node), &error));
    } else {
        throw GenicamException("Node '" + name + "' is not numeric.");
    }
    throwIfError(error);
    return value;
}

double Genicam::getFloatInc(const std::string& name) const {
    ArvGcNode* gc_node = node(name);
    GError* error = nullptr;
    double value = 0.0;
    if (ARV_IS_GC_FLOAT(gc_node)) {
        value = arv_gc_float_get_inc(ARV_GC_FLOAT(gc_node), &error);
    } else if (ARV_IS_GC_INTEGER(gc_node)) {
        value = static_cast<double>(arv_gc_integer_get_inc(ARV_GC_INTEGER(gc_node), &error));
    } else {
        throw GenicamException("Node '" + name + "' is not numeric.");
    }
    if (error != nullptr) {
        // An unspecified increment is not an error here.
        g_error_free(error);
        return 0.0;
    }
    return std::isfinite(value) && value > 1e-12 ? value : 0.0;
}

int64_t Genicam::getInt(const std::string& name) const { return static_cast<int64_t>(std::llround(getFloat(name))); }

void Genicam::setInt(const std::string& name, int64_t value) const { setFloat(name, static_cast<double>(value)); }

int64_t Genicam::getIntMin(const std::string& name) const {
    return static_cast<int64_t>(std::llround(getFloatMin(name)));
}

int64_t Genicam::getIntMax(const std::string& name) const {
    return static_cast<int64_t>(std::llround(getFloatMax(name)));
}

int64_t Genicam::getIntInc(const std::string& name) const {
    const int64_t inc = static_cast<int64_t>(std::llround(getFloatInc(name)));
    return inc > 0 ? inc : 1;
}

std::string Genicam::getEnum(const std::string& name) const {
    ArvGcNode* gc_node = node(name);
    if (!ARV_IS_GC_ENUMERATION(gc_node)) {
        throw GenicamException("Node '" + name + "' is not an enumeration.");
    }
    GError* error = nullptr;
    const char* value = arv_gc_enumeration_get_string_value(ARV_GC_ENUMERATION(gc_node), &error);
    throwIfError(error);
    return value != nullptr ? value : "";
}

void Genicam::setEnum(const std::string& name, const std::string& value) const {
    requireWritable(name);
    ArvGcNode* gc_node = node(name);
    if (!ARV_IS_GC_ENUMERATION(gc_node)) {
        throw GenicamException("Node '" + name + "' is not an enumeration.");
    }
    bool known = false;
    for (const std::string& entry : enumEntries(name)) {
        known = known || entry == value;
    }
    if (!known) {
        // Matches the wording GenApi uses, which the node's callbacks look for ("EnumEntry").
        throw GenicamException("EnumEntry '" + value + "' of node '" + name + "' is not available.");
    }
    GError* error = nullptr;
    arv_gc_enumeration_set_string_value(ARV_GC_ENUMERATION(gc_node), value.c_str(), &error);
    throwIfError(error);
}

std::vector<std::string> Genicam::enumEntries(const std::string& name) const {
    ArvGcNode* gc_node = node(name);
    if (!ARV_IS_GC_ENUMERATION(gc_node)) {
        throw GenicamException("Node '" + name + "' is not an enumeration.");
    }
    GError* error = nullptr;
    guint n_values = 0;
    const char** values =
        arv_gc_enumeration_dup_available_string_values(ARV_GC_ENUMERATION(gc_node), &n_values, &error);
    throwIfError(error);
    std::vector<std::string> entries;
    for (guint i = 0; i < n_values; ++i) {
        entries.emplace_back(values[i]);
    }
    g_free(values);
    return entries;
}

bool Genicam::getBool(const std::string& name) const {
    ArvGcNode* gc_node = node(name);
    if (!ARV_IS_GC_BOOLEAN(gc_node)) {
        throw GenicamException("Node '" + name + "' is not a boolean.");
    }
    GError* error = nullptr;
    const bool value = arv_gc_boolean_get_value(ARV_GC_BOOLEAN(gc_node), &error);
    throwIfError(error);
    return value;
}

void Genicam::setBool(const std::string& name, bool value) const {
    requireWritable(name);
    ArvGcNode* gc_node = node(name);
    if (!ARV_IS_GC_BOOLEAN(gc_node)) {
        throw GenicamException("Node '" + name + "' is not a boolean.");
    }
    GError* error = nullptr;
    arv_gc_boolean_set_value(ARV_GC_BOOLEAN(gc_node), value, &error);
    throwIfError(error);
}

std::string Genicam::getString(const std::string& name) const {
    ArvGcNode* gc_node = node(name);
    if (!ARV_IS_GC_STRING(gc_node)) {
        throw GenicamException("Node '" + name + "' is not a string.");
    }
    GError* error = nullptr;
    const char* value = arv_gc_string_get_value(ARV_GC_STRING(gc_node), &error);
    throwIfError(error);
    return value != nullptr ? value : "";
}

void Genicam::execute(const std::string& name) const {
    requireWritable(name);
    ArvGcNode* gc_node = node(name);
    if (!ARV_IS_GC_COMMAND(gc_node)) {
        throw GenicamException("Node '" + name + "' is not a command.");
    }
    GError* error = nullptr;
    arv_gc_command_execute(ARV_GC_COMMAND(gc_node), &error);
    throwIfError(error);
}

std::string Genicam::getValueAsString(const std::string& name) const {
    GError* error = nullptr;
    const char* value = arv_gc_feature_node_get_value_as_string(ARV_GC_FEATURE_NODE(node(name)), &error);
    throwIfError(error);
    return value != nullptr ? value : "";
}

void Genicam::setValueFromString(const std::string& name, const std::string& value) const {
    requireWritable(name);
    GError* error = nullptr;
    arv_gc_feature_node_set_value_from_string(ARV_GC_FEATURE_NODE(node(name)), value.c_str(), &error);
    throwIfError(error);
}

std::vector<std::string> Genicam::persistentFeatures() const {
    std::vector<std::string> features;
    ArvGc* genicam = arv_device_get_genicam(device_);
    std::function<void(const char*)> visit = [&](const char* name) {
        ArvGcNode* gc_node = arv_gc_get_node(genicam, name);
        if (gc_node == nullptr) {
            return;
        }
        if (ARV_IS_GC_CATEGORY(gc_node)) {
            for (const GSList* it = arv_gc_category_get_features(ARV_GC_CATEGORY(gc_node)); it != nullptr;
                 it = it->next) {
                visit(static_cast<const char*>(it->data));
            }
            return;
        }
        const bool is_value = ARV_IS_GC_FLOAT(gc_node) || ARV_IS_GC_INTEGER(gc_node) ||
                              ARV_IS_GC_ENUMERATION(gc_node) || ARV_IS_GC_BOOLEAN(gc_node) || ARV_IS_GC_STRING(gc_node);
        if (is_value && isReadable(name) && isWritable(name)) {
            features.emplace_back(name);
        }
    };
    visit("Root");
    return features;
}

}  // namespace basler_driver
