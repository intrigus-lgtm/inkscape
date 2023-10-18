# SPDX-License-Identifier: GPL-2.0-or-later
#! /bin/bash

cd src

function add() {
    if [ -n "$regex" ]; then
        regex+=";"
    fi
    regex+="$1"
}

regex=''
add 's/et_relief\s*(Gtk::RELIEF_NONE)/et_has_frame(false)/g'
add 's/et_relief\s*(Gtk::RELIEF_NORMAL)/et_has_frame(true)/g'
add 's/set_shadow_type\s*(Gtk::SHADOW_IN)/set_has_frame(true)/g'
add 's/set_shadow_type\s*(Gtk::SHADOW_ETCHED_IN)/set_has_frame(true)/g'
add 's/set_shadow_type\s*(Gtk::SHADOW_OUT)/set_has_frame(true)/g'
add 's/set_shadow_type\s*(Gtk::SHADOW_ETCHED_OUT)/set_has_frame(true)/g'
add 's/set_shadow_type\s*(Gtk::SHADOW_NONE)/set_has_frame(false)/g'
add 's/et_can_focus/et_focusable/g'
add 's/property_expand() = \(true\|false\)/set_expand(\1)/g'
add 's/property_expand().set_value(\(true\|false\))/set_expand(\1)/g'

find ../src -name '*.cpp' -or -name '*.h' | xargs sed -i "$regex"

regex=''

add 's/<property name="relief">none<\/property>/<property name="has-frame">false<\/property>/g'
add 's/<property name="shadow-type">none<\/property>/<property name="has-frame">false<\/property>/g'
add 's/<property name="shadow-type">in<\/property>/<property name="has-frame">true<\/property>/g'

add '/\s*<property name="visible">True<\/property>\s*/d'
add '/\s*<property name="no-show-all">.*<\/property>\s*/d'
add '/\s*<property name="constrain-to">.*<\/property>\s*/d'

add 's/<requires lib="gtk+" version="3.[0-9]*"\s*\/>/<requires lib="gtk" version="4.0"\/>/g'
add 's/<property name="can\(-\|_\)focus">/<property name="focusable">/g'

find ../share -name '*.ui' -or -name '*.glade' | xargs sed -i "$regex"
sed -i "$regex" 'inkview-window.cpp'
