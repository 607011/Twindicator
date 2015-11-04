# Copyright (c) 2015 Oliver Lau <ola@ct.de>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


include(PRIVATE.pri)
DEFINES += \
  TWEENDER_VERSION=\\\"$${TWEENDER_VERSION}\\\" \
  MY_CLIENT_KEY=\\\"$${MY_CLIENT_KEY}\\\" \
  MY_CLIENT_SECRET=\\\"$${MY_CLIENT_SECRET}\\\"

QT       += core gui network

include(3rdparty/oauth/src.pri)

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = Tweender
TEMPLATE = app

INCLUDEPATH += 3rdparty/oauth

SOURCES += main.cpp \
        mainwindow.cpp

HEADERS  += mainwindow.h

FORMS    += mainwindow.ui

