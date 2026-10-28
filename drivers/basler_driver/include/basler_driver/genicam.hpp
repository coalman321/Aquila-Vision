#pragma once

#include <arv.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace basler_driver {

// Thrown by every Genicam accessor on failure, the way GenApi throws GenICam::GenericException:
// the camera layer turns what() into the error string a service responds with.
class GenicamException : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

// Throws a GenicamException with the GError's message (freeing it) if one was set.
void throwIfError(GError* error);

// Typed access to an opened device's GenICam features by name. Numeric getters/setters accept both
// Float and Integer nodes, since Basler models disagree on which one a feature is (e.g. GainRaw vs Gain).
class Genicam {
   public:
    explicit Genicam(ArvDevice* device);

    // Exists, is implemented and is currently available (never throws).
    bool isAvailable(const std::string& name) const;
    bool isReadable(const std::string& name) const;
    bool isWritable(const std::string& name) const;

    double getFloat(const std::string& name) const;
    void setFloat(const std::string& name, double value) const;
    double getFloatMin(const std::string& name) const;
    double getFloatMax(const std::string& name) const;
    // 0 when the feature has no increment.
    double getFloatInc(const std::string& name) const;

    int64_t getInt(const std::string& name) const;
    void setInt(const std::string& name, int64_t value) const;
    int64_t getIntMin(const std::string& name) const;
    int64_t getIntMax(const std::string& name) const;
    int64_t getIntInc(const std::string& name) const;

    std::string getEnum(const std::string& name) const;
    void setEnum(const std::string& name, const std::string& value) const;
    // Entries that are currently available, by symbolic name.
    std::vector<std::string> enumEntries(const std::string& name) const;

    bool getBool(const std::string& name) const;
    void setBool(const std::string& name, bool value) const;

    std::string getString(const std::string& name) const;
    void execute(const std::string& name) const;

    std::string getValueAsString(const std::string& name) const;
    void setValueFromString(const std::string& name, const std::string& value) const;

    // Every readable and writable value feature reachable from the Root category, in tree order:
    // what a feature persistence (.pfs) file holds.
    std::vector<std::string> persistentFeatures() const;

   private:
    ArvGcNode* node(const std::string& name) const;
    // Throws GenApi's "Node is not writable." for a feature that exists but can't be written now (for
    // example one locked while acquisition runs).
    void requireWritable(const std::string& name) const;

    ArvDevice* device_;
};

}  // namespace basler_driver
