// track_sim.cpp : insert one nav point per minute over 1 hour, then query the last 15 minutes.
//
// Usage:
//   track_sim [conninfo]          Backfill: inserts 61 points timestamped from 60 minutes ago
//                                 up to now (instant), then queries the last 15 minutes.
//   track_sim [conninfo] --live   Real time: inserts one point per minute for an hour
//                                 (Ctrl-C to stop), querying the last 15 minutes every 5 points.
//
// conninfo defaults to $GEODB_CONN, else "host=localhost dbname=geodb".
// Simulated points use category "track" so they are easy to clear:
//   DELETE FROM nav_points WHERE category = 'track';
#include "geodb.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <pqxx/pqxx>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMetersPerDegLat = 111320.0;

// Format a time_t as a PostgreSQL timestamptz literal in UTC.
std::string utcStamp(std::time_t t) {
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[40];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S+00", &tm);
    return buf;
}

// A simple simulated vessel: ~5 m/s, heading slowly swinging around east.
struct Sim {
    double lon = -71.4812;  // start near Point Judith, RI
    double lat = 41.3614;
    double speed = 5.0;     // m/s

    double headingAt(int minute) const {
        return std::fmod(90.0 + 40.0 * std::sin(minute * 0.15) + 360.0, 360.0);
    }

    // Advance one minute at the given heading.
    void step(double heading_deg) {
        double d = speed * 60.0;
        double h = heading_deg * kPi / 180.0;
        lat += d * std::cos(h) / kMetersPerDegLat;
        lon += d * std::sin(h) / (kMetersPerDegLat * std::cos(lat * kPi / 180.0));
    }
};

geodb::NavPoint makePoint(const Sim& s, int minute, double heading, const std::string& when) {
    geodb::NavPoint p;
    p.name = "sim-" + std::string(minute < 10 ? "0" : "") + std::to_string(minute);
    p.category = "track";
    p.lon = s.lon;
    p.lat = s.lat;
    p.heading_deg = heading;
    p.speed_mps = s.speed;
    p.recorded_at = when;  // empty = let the database use now()
    return p;
}

// The "last 15 minutes" query, written directly as SQL with libpqxx.
// To change the question, edit the SQL string below (values go in as $1, $2, ...).
void printLast15(const std::string& conninfo) {
    pqxx::connection conn(conninfo);
    pqxx::work tx(conn);

    pqxx::result res = tx.exec_params(
        "SELECT name, "
        "       to_char(recorded_at, 'HH24:MI:SS') AS at, "
        "       ST_X(geom::geometry) AS lon, "
        "       ST_Y(geom::geometry) AS lat, "
        "       heading_deg, speed_mps, "
        "       round((extract(epoch FROM (now() - recorded_at)) / 60)::numeric, 1) AS mins_ago "
        "FROM nav_points "
        "WHERE category = $1 "
        "  AND recorded_at >= now() - ($2::int * interval '1 minute') "
        "ORDER BY recorded_at",
        std::string("track"), 15);

    std::cout << "\nPoints recorded in the last 15 minutes: " << res.size() << "\n\n";
    std::cout << std::left << std::setw(9) << "name" << std::setw(10) << "time"
              << std::right << std::setw(11) << "lon" << std::setw(11) << "lat"
              << std::setw(9) << "hdg" << std::setw(9) << "m/s"
              << std::setw(11) << "mins ago" << "\n";
    std::cout << std::string(70, '-') << "\n";

    std::cout << std::fixed;
    for (const auto& row : res) {
        std::cout << std::left << std::setw(9) << row["name"].as<std::string>()
                  << std::setw(10) << row["at"].as<std::string>()
                  << std::right << std::setprecision(5)
                  << std::setw(11) << row["lon"].as<double>()
                  << std::setw(11) << row["lat"].as<double>()
                  << std::setprecision(0)
                  << std::setw(9) << (row["heading_deg"].is_null() ? 0.0 : row["heading_deg"].as<double>())
                  << std::setprecision(1)
                  << std::setw(9) << (row["speed_mps"].is_null() ? 0.0 : row["speed_mps"].as<double>())
                  << std::setw(11) << row["mins_ago"].as<double>() << "\n";
    }
    std::cout << std::defaultfloat;
}

}  // namespace

int main(int argc, char** argv) {
    std::string conninfo = "host=localhost dbname=geodb";
    bool live = false;
    if (const char* env = std::getenv("GEODB_CONN")) conninfo = env;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--live") live = true;
        else conninfo = a;
    }

    try {
        geodb::Database db(conninfo);
        Sim sim;

        if (!live) {
            // Backfill: minute 0 is 60 minutes ago, minute 60 is now.
            std::time_t now = std::time(nullptr);
            for (int m = 0; m <= 60; ++m) {
                double hdg = sim.headingAt(m);
                std::time_t when = now - (60 - m) * 60;
                db.addNavPoint(makePoint(sim, m, hdg, utcStamp(when)));
                sim.step(hdg);
            }
            std::cout << "Inserted 61 points spanning the last hour.\n";
            printLast15(conninfo);
        } else {
            std::cout << "Live mode: one point per minute for 60 minutes. Ctrl-C to stop.\n";
            for (int m = 0; m <= 60; ++m) {
                double hdg = sim.headingAt(m);
                db.addNavPoint(makePoint(sim, m, hdg, ""));  // database timestamps it
                std::cout << "inserted sim-" << m << "\n";
                sim.step(hdg);
                if (m % 5 == 4) printLast15(conninfo);
                if (m < 60) std::this_thread::sleep_for(std::chrono::minutes(1));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}