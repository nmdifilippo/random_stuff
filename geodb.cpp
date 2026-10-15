// geodb.cpp : implementation using libpqxx (7.x).
#include "geodb.hpp"

#include <pqxx/pqxx>

namespace geodb {

namespace {

// Column list shared by all nav_points SELECTs; must match rowToNavPoint().
// The last column (distance_m) is supplied by each query.
const char* kNavCols =
    "id, name, category, ST_X(geom::geometry) AS lon, ST_Y(geom::geometry) AS lat, "
    "elevation_m, heading_deg, speed_mps, notes, contact_id, recorded_at::text";

template <typename T>
std::optional<T> opt(const pqxx::field& f) {
    if (f.is_null()) return std::nullopt;
    return f.as<T>();
}

NavPoint rowToNavPoint(const pqxx::row& r, bool hasDistance) {
    NavPoint p;
    p.id          = r[0].as<long long>();
    p.name        = r[1].as<std::string>();
    p.category    = r[2].as<std::string>();
    p.lon         = r[3].as<double>();
    p.lat         = r[4].as<double>();
    p.elevation_m = opt<double>(r[5]);
    p.heading_deg = opt<double>(r[6]);
    p.speed_mps   = opt<double>(r[7]);
    p.notes       = opt<std::string>(r[8]);
    p.contact_id  = opt<long long>(r[9]);
    p.recorded_at = r[10].as<std::string>();
    if (hasDistance) p.distance_m = r[11].as<double>();
    return p;
}

Contact rowToContact(const pqxx::row& r) {
    Contact c;
    c.id    = r[0].as<long long>();
    c.name  = r[1].as<std::string>();
    c.email = opt<std::string>(r[2]);
    c.phone = opt<std::string>(r[3]);
    c.notes = opt<std::string>(r[4]);
    return c;
}

}  // namespace

Database::Database(const std::string& conninfo)
    : conn_(std::make_unique<pqxx::connection>(conninfo)) {}

Database::~Database() = default;

// ---- Contacts --------------------------------------------------------------

long long Database::addContact(const Contact& c) {
    pqxx::work tx(*conn_);
    pqxx::result res = tx.exec_params(
        "INSERT INTO contacts (name, email, phone, notes) "
        "VALUES ($1, $2, $3, $4) RETURNING id",
        c.name, c.email, c.phone, c.notes);
    tx.commit();
    return res[0][0].as<long long>();
}

std::optional<Contact> Database::getContact(long long id) {
    pqxx::work tx(*conn_);
    pqxx::result res = tx.exec_params(
        "SELECT id, name, email, phone, notes FROM contacts WHERE id = $1", id);
    if (res.empty()) return std::nullopt;
    return rowToContact(res[0]);
}

std::vector<Contact> Database::findContactsByName(const std::string& substring) {
    pqxx::work tx(*conn_);
    pqxx::result res = tx.exec_params(
        "SELECT id, name, email, phone, notes FROM contacts "
        "WHERE name ILIKE '%' || $1 || '%' ORDER BY name",
        substring);
    std::vector<Contact> out;
    for (const auto& r : res) out.push_back(rowToContact(r));
    return out;
}

// ---- Navigation data ---------------------------------------------------------

long long Database::addNavPoint(const NavPoint& p) {
    // If p.recorded_at is set (e.g. "2026-10-14 21:00:00+00") it is used,
    // otherwise the database assigns now().
    std::optional<std::string> ts;
    if (!p.recorded_at.empty()) ts = p.recorded_at;

    pqxx::work tx(*conn_);
    pqxx::result res = tx.exec_params(
        "INSERT INTO nav_points "
        "(name, category, geom, elevation_m, heading_deg, speed_mps, notes, contact_id, recorded_at) "
        "VALUES ($1, $2, ST_SetSRID(ST_MakePoint($3, $4), 4326)::geography, "
        "        $5, $6, $7, $8, $9, COALESCE($10::timestamptz, now())) RETURNING id",
        p.name, p.category, p.lon, p.lat,
        p.elevation_m, p.heading_deg, p.speed_mps, p.notes, p.contact_id, ts);
    tx.commit();
    return res[0][0].as<long long>();
}

std::optional<NavPoint> Database::getNavPoint(long long id) {
    pqxx::work tx(*conn_);
    pqxx::result res = tx.exec_params(
        std::string("SELECT ") + kNavCols + " FROM nav_points WHERE id = $1", id);
    if (res.empty()) return std::nullopt;
    return rowToNavPoint(res[0], false);
}

std::vector<NavPoint> Database::navPointsWithin(double lon, double lat, double radius_m) {
    pqxx::work tx(*conn_);
    pqxx::result res = tx.exec_params(
        std::string("SELECT ") + kNavCols + ", "
        "ST_Distance(geom, ST_SetSRID(ST_MakePoint($1, $2), 4326)::geography) AS distance_m "
        "FROM nav_points "
        "WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint($1, $2), 4326)::geography, $3) "
        "ORDER BY distance_m",
        lon, lat, radius_m);
    std::vector<NavPoint> out;
    for (const auto& r : res) out.push_back(rowToNavPoint(r, true));
    return out;
}

std::vector<NavPoint> Database::nearestNavPoints(double lon, double lat, int n) {
    pqxx::work tx(*conn_);
    pqxx::result res = tx.exec_params(
        std::string("SELECT ") + kNavCols + ", "
        "ST_Distance(geom, ST_SetSRID(ST_MakePoint($1, $2), 4326)::geography) AS distance_m "
        "FROM nav_points "
        "ORDER BY geom <-> ST_SetSRID(ST_MakePoint($1, $2), 4326)::geography "
        "LIMIT $3",
        lon, lat, n);
    std::vector<NavPoint> out;
    for (const auto& r : res) out.push_back(rowToNavPoint(r, true));
    return out;
}

std::vector<NavPoint> Database::navPointsInBox(double min_lon, double min_lat,
                                               double max_lon, double max_lat) {
    pqxx::work tx(*conn_);
    pqxx::result res = tx.exec_params(
        std::string("SELECT ") + kNavCols + ", 0.0 AS distance_m "
        "FROM nav_points "
        "WHERE geom::geometry && ST_MakeEnvelope($1, $2, $3, $4, 4326) "
        "ORDER BY recorded_at",
        min_lon, min_lat, max_lon, max_lat);
    std::vector<NavPoint> out;
    for (const auto& r : res) out.push_back(rowToNavPoint(r, true));
    return out;
}

std::vector<NavPoint> Database::navPointsSince(int minutes, const std::string& category) {
    pqxx::work tx(*conn_);
    pqxx::result res = tx.exec_params(
        std::string("SELECT ") + kNavCols + ", 0.0 AS distance_m "
        "FROM nav_points "
        "WHERE recorded_at >= now() - ($1::int * interval '1 minute') "
        "  AND ($2::text = '' OR category = $2::text) "
        "ORDER BY recorded_at",
        minutes, category);
    std::vector<NavPoint> out;
    for (const auto& r : res) out.push_back(rowToNavPoint(r, true));
    return out;
}

}  // namespace geodb