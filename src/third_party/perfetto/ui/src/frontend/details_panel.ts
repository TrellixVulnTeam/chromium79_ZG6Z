// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import * as m from 'mithril';

import {LogExists, LogExistsKey} from '../common/logs';

import {ChromeSliceDetailsPanel} from './chrome_slice_panel';
import {CounterDetailsPanel} from './counter_panel';
import {DragGestureHandler} from './drag_gesture_handler';
import {globals} from './globals';
import {HeapProfileDetailsPanel} from './heap_profile_panel';
import {LogPanel} from './logs_panel';
import {NotesEditorPanel} from './notes_panel';
import {AnyAttrsVnode, PanelContainer} from './panel_container';
import {SliceDetailsPanel} from './slice_panel';
import {ThreadStatePanel} from './thread_state_panel';

const UP_ICON = 'keyboard_arrow_up';
const DOWN_ICON = 'keyboard_arrow_down';
const DRAG_HANDLE_HEIGHT_PX = 28;
const DEFAULT_DETAILS_HEIGHT_PX = 230 + DRAG_HANDLE_HEIGHT_PX;

function hasLogs(): boolean {
  const data = globals.trackDataStore.get(LogExistsKey) as LogExists;
  return data && data.exists;
}

interface DragHandleAttrs {
  height: number;
  resize: (height: number) => void;
}

class DragHandle implements m.ClassComponent<DragHandleAttrs> {
  private dragStartHeight = 0;
  private height = 0;
  private resize: (height: number) => void = () => {};
  private isClosed = this.height <= DRAG_HANDLE_HEIGHT_PX;

  oncreate({dom, attrs}: m.CVnodeDOM<DragHandleAttrs>) {
    this.resize = attrs.resize;
    this.height = attrs.height;
    this.isClosed = this.height <= DRAG_HANDLE_HEIGHT_PX;
    const elem = dom as HTMLElement;
    new DragGestureHandler(
        elem,
        this.onDrag.bind(this),
        this.onDragStart.bind(this),
        this.onDragEnd.bind(this));
  }

  onupdate({attrs}: m.CVnodeDOM<DragHandleAttrs>) {
    this.resize = attrs.resize;
    this.height = attrs.height;
    this.isClosed = this.height <= DRAG_HANDLE_HEIGHT_PX;
  }

  onDrag(_x: number, y: number) {
    const newHeight = this.dragStartHeight + (DRAG_HANDLE_HEIGHT_PX / 2) - y;
    this.isClosed = Math.floor(newHeight) <= DRAG_HANDLE_HEIGHT_PX;
    this.resize(Math.floor(newHeight));
    globals.rafScheduler.scheduleFullRedraw();
  }

  onDragStart(_x: number, _y: number) {
    this.dragStartHeight = this.height;
  }

  onDragEnd() {}

  view() {
    const icon = this.isClosed ? UP_ICON : DOWN_ICON;
    const title = this.isClosed ? 'Show panel' : 'Hide panel';
    return m(
        '.handle',
        m('.handle-title', 'Current Selection'),
        m('i.material-icons',
          {
            onclick: () => {
              if (this.height === DRAG_HANDLE_HEIGHT_PX) {
                this.isClosed = false;
                this.resize(DEFAULT_DETAILS_HEIGHT_PX);
              } else {
                this.isClosed = true;
                this.resize(DRAG_HANDLE_HEIGHT_PX);
              }
              globals.rafScheduler.scheduleFullRedraw();
            },
            title
          },
          icon));
  }
}

export class DetailsPanel implements m.ClassComponent {
  private detailsHeight = DRAG_HANDLE_HEIGHT_PX;
  // Used to set details panel to default height on selection.
  private showDetailsPanel = true;

  view() {
    const detailsPanels: AnyAttrsVnode[] = [];
    const curSelection = globals.state.currentSelection;
    if (curSelection) {
      switch (curSelection.kind) {
        case 'NOTE':
          detailsPanels.push(m(NotesEditorPanel, {
            key: 'notes',
            id: curSelection.id,
          }));
          break;
        case 'SLICE':
          detailsPanels.push(m(SliceDetailsPanel, {
            key: 'slice',
          }));
          break;
        case 'COUNTER':
          detailsPanels.push(m(CounterDetailsPanel, {
            key: 'counter',
          }));
          break;
        case 'HEAP_PROFILE':
          detailsPanels.push(m(HeapProfileDetailsPanel, {key: 'heap_profile'}));
          break;
        case 'CHROME_SLICE':
          detailsPanels.push(m(ChromeSliceDetailsPanel));
          break;
        case 'THREAD_STATE':
          detailsPanels.push(m(ThreadStatePanel, {
            key: 'thread_state',
            ts: curSelection.ts,
            dur: curSelection.dur,
            utid: curSelection.utid,
            state: curSelection.state,
            cpu: curSelection.cpu
          }));
          break;
        default:
          break;
      }
    } else if (hasLogs()) {
      detailsPanels.push(m(LogPanel, {}));
    }

    const wasShowing = this.showDetailsPanel;
    this.showDetailsPanel = detailsPanels.length > 0;
    // Pop up details panel on first selection.
    if (!wasShowing && this.showDetailsPanel &&
        this.detailsHeight === DRAG_HANDLE_HEIGHT_PX) {
      this.detailsHeight = DEFAULT_DETAILS_HEIGHT_PX;
    }

    return m(
        '.details-content',
        {
          style: {
            height: `${this.detailsHeight}px`,
            display: this.showDetailsPanel ? null : 'none'
          }
        },
        m(DragHandle, {
          resize: (height: number) => {
            this.detailsHeight = Math.max(height, DRAG_HANDLE_HEIGHT_PX);
          },
          height: this.detailsHeight,
        }),
        m('.details-panel-container',
          m(PanelContainer,
            {doesScroll: true, panels: detailsPanels, kind: 'DETAILS'})));
  }
}
