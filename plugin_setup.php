<div id="global" class="settings">
<?php
PrintSettingGroup("APCSGeneral", "", "", 1, "fpp-artnet-prop-control-secondary");
PrintSettingGroup("APCSLetters", "", "", 1, "fpp-artnet-prop-control-secondary");
PrintSettingGroup("APCSFestoon", "", "", 1, "fpp-artnet-prop-control-secondary");
?>

<div class="alert alert-info mt-3" role="alert">
  <strong>Art-Net slots:</strong><br>
  1 Master; 2 Letters Brightness; 3 Letters Red; 4 Letters Green; 5 Letters Blue; 6 Letters Colour Mode;<br>
  7-9 spare;<br>
  10 Festoon A Brightness; 11 A Red; 12 A Green; 13 A Blue; 14 A Colour Mode — <strong>pixels 1,3,5...</strong><br>
  15-19 spare;<br>
  20 Festoon B Brightness; 21 B Red; 22 B Green; 23 B Blue; 24 B Colour Mode — <strong>pixels 2,4,6...</strong>
</div>

<div class="alert alert-success" role="alert">
  <strong>Colour mode for Ch 6, 14 and 24:</strong><br>
  <strong>0-127:</strong> Full source colour — keep the Sequence/Effect RGB while active.<br>
  <strong>128-255:</strong> Desk colour override — keep the Sequence/Effect pattern/intensity but recolour it with that group's Art-Net RGB controls.<br>
  With nothing running, each group outputs its desk-selected solid colour on only its assigned pixels.
</div>

<div class="alert alert-warning" role="alert">
  Use <strong>Bridge Data Priority = Prioritize Bridge</strong> so Art-Net changes remain live during playback.
  Map at least Art-Net slots 1-24 into the control block (default FPP 10001-10024).
  Channel 1 Master is applied last to Letters and both Festoon pixel sets.<br>
  If the original Art-Net Prop Control plugin is also installed, leave <strong>Bypass ON</strong> in one of the two plugins; do not let both process the same prop channels simultaneously.
</div>
</div>
