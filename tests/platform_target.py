"""Select hardware conditionals for host tests; the default remains Pi 3."""
import os

RASPPI = os.environ.get("PI1551_TEST_RASPPI", "3")
if RASPPI not in ("2", "3"):
    raise ValueError("PI1551_TEST_RASPPI must be 2 or 3")
RPI_DEFINE = "-DRPI" + RASPPI + "=1"
