// geodb.hpp : small C++ interface to the contacts / nav_points tables.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pqxx { class connection; }

namespace geodb {

struct Contact {
    long long id = 0;
    std::string name;
    std::optional<std::string> email;
    std::optional<std::string> phone;
    std::optional<std::string> notes;
};

struct NavPoint {
    long long id = 0;
    std::string name;
    std::string category = "waypoint";
    double lon = 0.0;                    // degrees, WGS84
    double lat = 0.0;                    // degrees, WGS84
    std::optional<double> elevation_m;
    std::optional<double> heading_deg;   // 0..360
    std::optional<double> speed_mps;
    std::optional<std::string> notes;
    std::optional<long long> contact_id;
    std::string recorded_at;             // filled on query; ISO timestamp
    double distance_m = 0.0;             // filled by spatial queries
};

class Database {
public:
    // e.g. "host=localhost dbname=geodb user=me password=secret"
    explicit Database(const std::string& conninfo);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Contacts
    long long addContact(const Contact& c);
    std::optional<Contact> getContact(long long id);
    std::vector<Contact> findContactsByName(const std::string& substring);

    // Navigation data
    long long addNavPoint(const NavPoint& p);
    std::optional<NavPoint> getNavPoint(long long id);
    // All points within radius_m meters of (lon, lat), nearest first.
    std::vector<NavPoint> navPointsWithin(double lon, double lat, double radius_m);
    // The n closest points to (lon, lat).
    std::vector<NavPoint> nearestNavPoints(double lon, double lat, int n);
    // Points inside a lon/lat bounding box.
    std::vector<NavPoint> navPointsInBox(double min_lon, double min_lat,
                                         double max_lon, double max_lat);
    // Points recorded in the last `minutes` minutes, oldest first.
    // Pass a category (e.g. "track") to filter, or leave empty for all.
    std::vector<NavPoint> navPointsSince(int minutes, const std::string& category = "");

private:
    std::unique_ptr<pqxx::connection> conn_;
};

}  // namespace geodb