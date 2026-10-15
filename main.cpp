// main.cpp : demo of the geodb interface.
#include "geodb.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    // Connection string from argv[1], else $GEODB_CONN, else a local default.
    std::string conninfo = "host=localhost dbname=geodb";
    if (argc > 1) conninfo = argv[1];
    else if (const char* env = std::getenv("GEODB_CONN")) conninfo = env;

    try {
        geodb::Database db(conninfo);

        geodb::Contact c;
        c.name = "Alice Mariner";
        c.email = "alice@example.com";
        long long cid = db.addContact(c);

        geodb::NavPoint p;
        p.name = "Point Judith Light";
        p.category = "landmark";
        p.lon = -71.4812;
        p.lat = 41.3614;
        p.elevation_m = 15.0;
        p.contact_id = cid;
        long long pid = db.addNavPoint(p);

        geodb::NavPoint q;
        q.name = "Block Island North";
        q.category = "waypoint";
        q.lon = -71.5800;
        q.lat = 41.2300;
        q.heading_deg = 270.0;
        q.speed_mps = 4.5;
        db.addNavPoint(q);

        std::cout << "Inserted contact " << cid << " and nav point " << pid << "\n";

        for (const auto& r : db.navPointsWithin(-71.50, 41.35, 25000.0))
            std::cout << r.name << "  " << r.distance_m / 1000.0 << " km\n";

        for (const auto& r : db.findContactsByName("alice"))
            std::cout << "Contact: " << r.name << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
