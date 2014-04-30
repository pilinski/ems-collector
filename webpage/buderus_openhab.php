<?php
include 'sensor_utils.php.inc';
include 'utils.php.inc';

set_loc_settings();

$sensors = get_current_sensor_values();
$changes = get_sensor_changes_for_day(0);

print( "RaumIstTemp="  . $sensors[SensorRaumIstTemp]  . "<br>");
print( "RaumSollTemp=" . $sensors[SensorRaumSollTemp] . "<br>");
print( "AussenTemp="   . $sensors[SensorAussenTemp]   . "<br>");
print( "SystemDruck="  . $sensors[SensorSystemdruck]  . "<br>");
print( "ServiceCode="  . $sensors[SensorServiceCode]  . "<br>");
print( "FehlerCode="   . $sensors[SensorFehlerCode]   . "<br>");
print( "WarmwasserIstTemp="   . $sensors[SensorWarmwasserIstTemp]    . "<br>");
print( "WarmwasserBereitung=" . ($sensors[SensorWarmwasserBereitung] ? "OPEN" : "CLOSED")  . "<br>");
$value = $sensors[SensorFlamme] ? "OPEN" : "CLOSED";
print( "Flamme="       . $value                       . "<br>");
?>
