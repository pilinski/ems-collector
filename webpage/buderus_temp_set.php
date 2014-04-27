<?php
include 'sensor_utils.php.inc';
include 'utils.php.inc';

set_loc_settings();

$sensors = get_current_sensor_values();
$changes = get_sensor_changes_for_day(0);

print( "ust=" . $sensors[SensorRaumSollTemp] );
?>
