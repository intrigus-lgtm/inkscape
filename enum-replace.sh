# SPDX-License-Identifier: GPL-2.0-or-later
#! /bin/bash

regex=''

function replace() {
    local old="$1"
    local new="$2"
    if [ -n "$regex" ]; then
        regex+=";"
    fi
    regex+="s/$old/$new/g"
}

function rename() {
    local namespace="$1"
    local old="$2"
    local new="$3"
    replace "$namespace::$old" "$namespace::$new"
}

function add() {
    local namespace="$1"
    local old="$2"
    local new="$3"
    rename "$namespace" "$new::${old}_" "$new::"
    rename "$namespace" "${old}_" "$new::"
}

add "Cairo" "FORMAT" "Surface::Format"
add "Cairo" "OPERATOR" "Context::Operator"
add "Cairo" "FILL_RULE" "Context::FillRule"
add "Cairo" "LINE_CAP" "Context::LineCap"
add "Cairo" "FONT_SLANT" "ToyFontFace::Slant"
add "Cairo" "FONT_WEIGHT" "ToyFontFace::Weight"
add "Cairo" "FILTER" "SurfacePattern::Filter"
add "Cairo" "EXTEND" "Pattern::Extend"
add "Cairo" "REGION_OVERLAP" "Region::Overlap"
add "Cairo" "LINE_JOIN" "Context::LineJoin"

add "Gtk" "SELECTION" "SelectionMode"
add "Gtk" "MESSAGE" "MessageType"
add "Gtk" "ORIENTATION" "Orientation"
add "Gtk" "ALIGN" "Align"
add "Gtk" "RESPONSE" "ResponseType"
add "Gtk" "BUTTONS" "ButtonsType"
add "Gtk" "WRAP" "WrapMode"
add "Gtk" "POLICY" "PolicyType"
add "Gtk" "FILE_CHOOSER_ACTION" "FileChooser::Action"
rename "Gtk" "FileChooserAction" "FileChooser::Action"
add "Gtk" "TEXT_DIR" "TextDirection"
add "Gtk" "SIZE_GROUP" "SizeGroup::Mode"
add "Gtk" "SORT" "SortType"
rename "Gtk" "TREE_VIEW_COLUMN_AUTOSIZE" "TreeViewColumn::Sizing::AUTOSIZE"
rename "Gtk" "TREE_VIEW_COLUMN_FIXED" "TreeViewColumn::Sizing::FIXED"
add "Gtk" "TREE_VIEW_DROP" "TreeView::DropPosition"
add "Gtk" "CELL_RENDERER_MODE" "CellRendererMode"
add "Gtk" "CELL_RENDERER" "CellRendererState"
add "Gtk" "JUSTIFY" "Justification"
rename "Gtk" "ResponseType::RESPONSE_ACCEPT" "ResponseType::ACCEPT"
add "Gtk" "STATE_FLAG" "StateFlags"
add "Gtk" "PHASE" "PropagationPhase"
rename "Gtk" "GestureMultiPress" "GestureClick"
add "Gtk" "EVENT_SEQUENCE" "EventSequenceState"
add "Gtk" "DIR" "DirectionType"
add "Gtk" "POS" "PositionType"

add "Gdk" "COLORSPACE" "Colorspace"
add "Gdk" "INTERP" "InterpType"
add "Gdk" "EVENT_SEQUENCE" "EventSequenceState"
add "Gdk" "AXIS" "AxisUse"
add "Gdk" "ACTION" "DragAction"
rename "Gdk" "BUTTON1_MASK" "ModifierType::BUTTON1_MASK"
add "Gdk" "SOURCE" "InputSource"
add "Gdk" "SEAT_CAPABILITY" "Seat::Capabilities"
add "GDK" "WINDOW_STATE" "Toplevel::State"

add "Glib" "RegexMatchFlags::REGEX_MATCH" "Regex::MatchFlags"
add "Glib" "REGEX_MATCH" "Regex::MatchFlags"
rename "Glib" "RegexMatchFlags" "Regex::MatchFlags"
add "Glib" "FILE_TEST" "FileTest"
rename "Glib" "REGEX_ANCHORED" "Regex::CompileFlags::ANCHORED"
rename "Glib" "REGEX_FIXED" "Regex::CompileFlags::FIXED"
rename "Glib" "REGEX_OPTIMIZE" "Regex::CompileFlags::OPTIMIZE"
rename "Glib" "Checksum" "ChecksumType"
add "Glib" "SPAWN" "SpawnFlags"

add "Gio" "APPLICATION" "Application::Flags"
add "Gio" "FILE_COPY" "File::CopyFlags"
rename "Gio" "FileMonitorEvent" "FileMonitor::Event"

add "Pango" "EllipsizeMode::ELLIPSIZE" "EllipsizeMode"
add "Pango" "ELLIPSIZE" "EllipsizeMode"
add "Pango" "STYLE" "Style"

replace "OPTION_TYPE_" "OptionType::"
add "T" "OPTION_TYPE" "OptionType"

replace "MOD1_MASK" "ALT_MASK"

function process_dir() {
    local path="$1"
    find "$path" -name '*.cpp' -or -name '*.h' | xargs sed -i "$regex"
}

process_dir "src"
process_dir "testfiles/src"
