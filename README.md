# Source keys

A row of keys with logos for the options of a select: PC, Mac, PlayStation, Switch. The key of the option chosen now is
the accent colour, and a tap switches to another one. With a script set, the tap runs that script with the option as a
field, so the script can wake a monitor and switch its input on the way.

## Set up

1. In Tessera, open **Plugins**, choose **Add with a link** and paste `https://github.com/Orishas/tessera-source-keys`.
2. Add the plugin to a screen. The screen builds once.
3. In **Layout**, place the **Source keys** tile from the library's Plugins group, two cells wide.
4. In the inspector, choose the select or input select whose options the keys show (such as
   `input_select.buro_monitor_quelle`).
5. Optional: choose a **Script** and its **Field** (such as `script.buro_quelle_anzeigen` and `quelle`). A tap then runs
   the script with the option, instead of choosing the option directly.
6. Optional: list the **Keys** yourself as `Option|logo`, one per line. Without, every option of the select gets a key
   and a logo guessed from its name.

Logos: `windows`, `apple`, `playstation`, `nintendo-switch`, `xbox`, `linux`. A key without a logo shows its word only.

## Good to know

- The screen must be allowed to perform Home Assistant actions (Tessera turns it on when it adds the screen).
- Up to six keys. On a screen that shows no pictures (the CYD, the Waveshare 3.5-inch, the Hosyond 4-inch) the keys
  show their words only.
- The logos are Material Design Icons (Apache 2.0), the ones Home Assistant uses too; the brands belong to their owners.

## Privacy

The logos are small PNG files in this repository. Tessera's app fetches them from `raw.githubusercontent.com` and sends
them to the screen. GitHub sees the address of your Home Assistant's internet connection and which logo was asked for;
nothing about your home, your entities or your choices leaves Home Assistant.
