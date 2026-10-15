-- schema.sql : creates the contacts and navigation tables
-- Usage:
--   createdb geodb
--   psql -d geodb -f schema.sql
-- (CREATE EXTENSION needs a role with sufficient privilege, e.g. postgres.)

CREATE EXTENSION IF NOT EXISTS postgis;

-- Basic contacts ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS contacts (
    id          BIGSERIAL PRIMARY KEY,
    name        TEXT        NOT NULL,
    email       TEXT,
    phone       TEXT,
    notes       TEXT,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS contacts_name_idx ON contacts (lower(name));

-- Navigation data -----------------------------------------------------------
-- geom is WGS84 lon/lat stored as geography, so distances are in meters.
CREATE TABLE IF NOT EXISTS nav_points (
    id            BIGSERIAL PRIMARY KEY,
    name          TEXT        NOT NULL,
    category      TEXT        NOT NULL DEFAULT 'waypoint',  -- waypoint, track, landmark, ...
    geom          GEOGRAPHY(Point, 4326) NOT NULL,
    elevation_m   DOUBLE PRECISION,
    heading_deg   DOUBLE PRECISION CHECK (heading_deg IS NULL OR (heading_deg >= 0 AND heading_deg < 360)),
    speed_mps     DOUBLE PRECISION CHECK (speed_mps IS NULL OR speed_mps >= 0),
    recorded_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    notes         TEXT,
    contact_id    BIGINT REFERENCES contacts(id) ON DELETE SET NULL  -- optional link
);

CREATE INDEX IF NOT EXISTS nav_points_geom_idx     ON nav_points USING GIST (geom);
CREATE INDEX IF NOT EXISTS nav_points_time_idx     ON nav_points (recorded_at);
CREATE INDEX IF NOT EXISTS nav_points_category_idx ON nav_points (category);
