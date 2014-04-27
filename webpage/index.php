<html>

<head>
  <meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
  <title>Heizung</title>

  <link href="style.css" type="text/css" rel="stylesheet"/>

  <script Language="JavaScript">
    function update() {
      window.location.reload();
      setTimeout("update()", 30000);
    }

    window.onload = function () {
      setTimeout("update()", 30000);
    }
  </script>
</head>

<?php
include 'sensor_utils.php.inc';
include 'utils.php.inc';

set_loc_settings();
?>
<?php
$sensors = get_current_sensor_values();	
$changes = get_sensor_changes_for_day(0);
?>
<?php
function print_header($name) {
  print "<table border=1 cellspacing=3 cellpadding=2 width=\"100%\">\n";
  print "<tr><td colspan=2 style=\"background-color: rgb(102,153,204);\" height=21>\n";
  print "<p><b><span style=\"font-size: medium; color: rgb(255,255,255);\">" . $name . "</span></b></p>\n";
  print "</td></tr>\n";
}

function print_cell($name, $value, $color = "") {
  print "<tr>\n";
  if ($color == "green") {
    $color = " style=\"background-color: rgb(0,200,0); color: rgb(255,255,255);\"";
  } else if ($color == "red") {
    $color = " style=\"background-color: rgb(200,0,0); color: rgb(255,255,255);\"";
  } else if ($color == "yellow") {
    $color = " style=\"background-color: rgb(200,200,0); color: rgb(255,255,255);\"";
  } else {
    $color = "";
  }

  print "<td width=147 height=18><p>" . $name . "</p></td>\n";
  print "<td width=129 align=center><p><span" . $color . ">" . $value . "</span></p></td>\n";
  print "</tr>\n";
}
?>

<body topmargin=0 leftmargin=0 marginwidth=0 marginheight=0>
<?php
print "STATUS<br>" ;
?>
  <table border=0 cellspacing=0 cellpadding=0 style="width:800; text-align:center;">
    <tr><td width="100%">
      <h2>
<?
print "Momentaner Status (" . format_timestamp(time(), TRUE);
print ")" ;
?>
	</h2>
    </td></tr>
    <tr><td>
      <?php include "menu.inc"; ?>
    </td></tr>
    <tr height=10></tr>
    <tr><td>
      <table border=1>
        <tr valign="top">
          <td width=390>
            <?php
              print_header("Heizung");
              print_cell("Kessel IST", $sensors[SensorKesselIstTemp]);
              print_cell("Kessel SOLL", $sensors[SensorKesselSollTemp]);
              $value = $sensors[SensorKesselPumpe] && !$sensors[Sensor3WegeVentil];
              print_cell("Vorlaufpumpe", $value ? "- an -" : "- aus -", $value ? "green" : "");
              $value = $sensors[SensorBrenner];
              print_cell("Brenner",
                         $value ? ($sensors[SensorWarmwasserBereitung] ? "WW-Bereitung" : "Heizen") : "- aus -",
                         $value ? "red" : "");
              $value = $sensors[SensorFlamme] ? " - an, " . $sensors[SensorFlammenstrom] . " -" : " - aus -";
              print_cell("Flamme", $value, $sensors[SensorFlamme] ? "red" : "");
              $value = $sensors[SensorMomLeistung] . " / " . $sensors[SensorMaxLeistung];
              print_cell("Momentane Leistung", $value);
              print_cell("Sommerbetrieb", $sensors[SensorSommerbetrieb] ? "- aktiv -" : "- inaktiv -");
            ?>
            </table>
          </td>
          <td width=20></td>
          <td width=390>
            <?php
              print_header("Heizkreise");
              $value = $sensors[SensorVorlaufHK1SollTemp] . " / " . $sensors[SensorVorlaufHK1IstTemp];
              print_cell("Heizkreis 1 Soll/Ist", $value);
              if ($sensors[SensorHK1Party]) {
                $value = "Party";
              } else if ($sensors[SensorHK1Ferien]) {
                $value = "Ferien";
              } else {
                $value = ($sensors[SensorHK1Automatik] ? "Auto" : "Manuell") . " (" .
                         ($sensors[SensorHK1Tagbetrieb] ? "Tag" : "Nacht") . ")";
              }
              print_cell("Betriebsart HK1", $value);
              print_cell("Pumpe HK1",
                         $sensors[SensorHK1Pumpe] ? "- aktiv -" : "- inaktiv -", 
                         $sensors[SensorHK1Pumpe] ? "green" : "");
              $value = $sensors[SensorVorlaufHK2SollTemp] . " / " . $sensors[SensorVorlaufHK2IstTemp];
              print_cell("Heizkreis 2 Soll/Ist", $value);
              if ($sensors[SensorHK2Party]) {
                $value = "Party";
              } else if ($sensors[SensorHK2Ferien]) {
                $value = "Ferien";
              } else {
                $value = ($sensors[SensorHK2Automatik] ? "Auto" : "Manuell") . " (" .
                         ($sensors[SensorHK2Tagbetrieb] ? "Tag" : "Nacht") . ")";
              }
              print_cell("Betriebsart HK2", $value);
              print_cell("Pumpe HK2",
                         $sensors[SensorHK2Pumpe] ? "- aktiv -" : "- inaktiv -", 
                         $sensors[SensorHK2Pumpe] ? "green" : "");
              print_cell("Mischersteuerung HK2", $sensors[SensorMischersteuerung]);
              print_cell("Rücklauf IST", $sensors[SensorRuecklaufTemp]);
            ?>
            </table>
          </td>
        </tr>
        <tr height=6></tr>
        <tr valign="top">
          <td width=390>
            <?php
              print_header("Warmwasser");
              print_cell("Warmwasser IST", $sensors[SensorWarmwasserIstTemp],
                         $sensors[SensorWarmwasserTempOK] ? "" : "yellow");
              print_cell("Warmwasser SOLL", $sensors[SensorWarmwasserSollTemp]);
              $value = $sensors[SensorWWTagbetrieb] ? "Tag" : "Nacht";
              print_cell("Betriebsart", $value);
              $value = $sensors[SensorKesselPumpe] && $sensors[Sensor3WegeVentil];
              print_cell("WW-Pumpe", $value ? "- an -" : "- aus -", $value ? "green" : "");
              $value = $sensors[SensorZirkulation];
              print_cell("Zirkulationspumpe", $value ? "- an -" : "- aus -", $value ? "green" : "");
              print_cell("WW-Vorrang", $sensors[SensorWWVorrang] ? "- an -" : "- aus -");
            ?>
            </table>
          </td>
          <td width=20></td>
          <td width=390>
            <?php
              print_header("Sonstige Temperaturen");
              print_cell("Außen", $sensors[SensorAussenTemp]);
              print_cell("Außen gedämpft", $sensors[SensorGedaempfteAussenTemp]);
              print_cell("Raumtemp. IST", $sensors[SensorRaumIstTemp]);
              print_cell("Raumtemp. SOLL", $sensors[SensorRaumSollTemp]);
            ?>
            </table>
          </td>
        </tr>
        <tr height=6></tr>
        <tr valign="top">
          <td width=390>
            <?php
              print_header("Heutige Aktivität");
              print_cell("Brennerlaufzeit", $changes[SensorBetriebszeit]);
              print_cell("Brennerstarts", $changes[SensorBrennerstarts]);
              print_cell("Heizungs-Brennerlaufzeit", $changes[SensorHeizZeit]);
              print_cell("Warmwasserbereitungszeit", $changes[SensorWarmwasserbereitungsZeit]);
              print_cell("Warmwasserbereitungen", $changes[SensorWarmwasserBereitungen]);
            ?>
            </table>
          </td>
          <td width=20></td>
          <td width=390>
            <?php
              print_header("Betriebsstatus");
              print_cell("Brennerlaufzeit", $sensors[SensorBetriebszeit]);
              print_cell("Brennerstarts", $sensors[SensorBrennerstarts]);
              print_cell("Systemdruck", $sensors[SensorSystemdruck]);
              print_cell("Servicecode", $sensors[SensorServiceCode]);
              print_cell("Fehlercode", $sensors[SensorFehlerCode]);
              # TODO: Fehler
            ?>
            </table>
          </td>
        </tr>
      </table>
    </td></tr>
  </table>
KONIEC
</body>

</html>

