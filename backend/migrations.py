"""Additive upgrade for databases created before battery telemetry existed."""
from sqlalchemy import inspect, text


def upgrade_telemetry(engine):
    with engine.begin() as connection:
        # Serialize upgrades across PostgreSQL application workers.
        if connection.dialect.name == "postgresql":
            connection.execute(text("SELECT pg_advisory_xact_lock(5302026)"))
        columns = {column["name"] for column in
                   inspect(connection).get_columns("solar_telemetry")}
        if "battery_voltage" not in columns:
            connection.execute(text(
                "ALTER TABLE solar_telemetry ADD COLUMN battery_voltage FLOAT"
            ))
        if "local_hold" not in columns:
            connection.execute(text(
                "ALTER TABLE solar_telemetry ADD COLUMN local_hold BOOLEAN "
                "NOT NULL DEFAULT FALSE"
            ))
