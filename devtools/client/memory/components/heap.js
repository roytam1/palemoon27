/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const { DOM: dom, createClass, PropTypes, createFactory } = require("devtools/client/shared/vendor/react");
const { assert, safeErrorString } = require("devtools/shared/DevToolsUtils");
const Census = createFactory(require("./census"));
const CensusHeader = createFactory(require("./census-header"));
const DominatorTree = createFactory(require("./dominator-tree"));
const DominatorTreeHeader = createFactory(require("./dominator-tree-header"));
const TreeMap = createFactory(require("./tree-map"));
const HSplitBox = createFactory(require("devtools/client/shared/components/h-split-box"));
const ShortestPaths = createFactory(require("./shortest-paths"));
const { getStatusTextFull, L10N } = require("../utils");
const {
  snapshotState: states,
  diffingState,
  viewState,
  censusState,
  treeMapState,
  dominatorTreeState
} = require("../constants");
const { snapshot: snapshotModel, diffingModel } = require("../models");

/**
 * Get the app state's current state atom.
 *
 * @see the relevant state string constants in `../constants.js`.
 *
 * @param {viewState} view
 * @param {snapshotModel} snapshot
 * @param {diffingModel} diffing
 *
 * @return {snapshotState|diffingState|dominatorTreeState}
 */
function getState(view, snapshot, diffing) {
  switch (view) {
    case viewState.CENSUS:
      return snapshot.census
        ? snapshot.census.state
        : snapshot.state;

    case viewState.DIFFING:
      return diffing.state;

    case viewState.TREE_MAP:
    return snapshot.treeMap
      ? snapshot.treeMap.state
      : snapshot.state;

    case viewState.DOMINATOR_TREE:
      return snapshot.dominatorTree
        ? snapshot.dominatorTree.state
        : snapshot.state;
  }

  assert(false, `Unexpected view state: ${view}`);
  return null;
}

/**
 * Return true if we should display a status message when we are in the given
 * state. Return false otherwise.
 *
 * @param {snapshotState|diffingState|dominatorTreeState} state
 * @param {viewState} view
 * @param {snapshotModel} snapshot
 *
 * @returns {Boolean}
 */
function shouldDisplayStatus(state, view, snapshot) {
  switch (state) {
    case states.IMPORTING:
    case states.SAVING:
    case states.SAVED:
    case states.READING:
    case censusState.SAVING:
    case treeMapState.SAVING:
    case diffingState.SELECTING:
    case diffingState.TAKING_DIFF:
    case dominatorTreeState.COMPUTING:
    case dominatorTreeState.COMPUTED:
    case dominatorTreeState.FETCHING:
      return true;
  }
  return view === viewState.DOMINATOR_TREE && !snapshot.dominatorTree;
}

/**
 * Get the status text to display for the given state.
 *
 * @param {snapshotState|diffingState|dominatorTreeState} state
 * @param {diffingModel} diffing
 *
 * @returns {String}
 */
function getStateStatusText(state, diffing) {
  if (state === diffingState.SELECTING) {
    return L10N.getStr(diffing.firstSnapshotId === null
                         ? "diffing.prompt.selectBaseline"
                         : "diffing.prompt.selectComparison");
  }

  return getStatusTextFull(state);
}

/**
 * Given that we should display a status message, return true if we should also
 * display a throbber along with the status message. Return false otherwise.
 *
 * @param {diffingModel} diffing
 *
 * @returns {Boolean}
 */
function shouldDisplayThrobber(diffing) {
  return !diffing || diffing.state !== diffingState.SELECTING;
}

/**
 * Get the current state's error, or return null if there is none.
 *
 * @param {snapshotModel} snapshot
 * @param {diffingModel} diffing
 *
 * @returns {Error|null}
 */
function getError(snapshot, diffing) {
  if (diffing) {
    if (diffing.state === diffingState.ERROR) {
      return diffing.error;
    }
    if (diffing.census === censusState.ERROR) {
      return diffing.census.error;
    }
  }

  if (snapshot) {
    if (snapshot.state === states.ERROR) {
      return snapshot.error;
    }

    if (snapshot.census === censusState.ERROR) {
      return snapshot.census.error;
    }

    if (snapshot.treeMap === treeMapState.ERROR) {
      return snapshot.treeMap.error;
    }

    if (snapshot.dominatorTree &&
        snapshot.dominatorTree.state === dominatorTreeState.ERROR) {
      return snapshot.dominatorTree.error;
    }
  }

  return null;
}

/**
 * Main view for the memory tool.
 *
 * The Heap component contains several panels for different states; an initial
 * state of only a button to take a snapshot, loading states, the census view
 * tree, the dominator tree, etc.
 */
const Heap = module.exports = createClass({
  displayName: "Heap",

  propTypes: {
    onSnapshotClick: PropTypes.func.isRequired,
    onLoadMoreSiblings: PropTypes.func.isRequired,
    onCensusExpand: PropTypes.func.isRequired,
    onCensusCollapse: PropTypes.func.isRequired,
    onDominatorTreeExpand: PropTypes.func.isRequired,
    onDominatorTreeCollapse: PropTypes.func.isRequired,
    onCensusFocus: PropTypes.func.isRequired,
    onDominatorTreeFocus: PropTypes.func.isRequired,
    onShortestPathsResize: PropTypes.func.isRequired,
    snapshot: snapshotModel,
    onViewSourceInDebugger: PropTypes.func.isRequired,
    diffing: diffingModel,
    view: PropTypes.string.isRequired,
    sizes: PropTypes.object.isRequired,
  },

  render() {
    let {
      snapshot,
      diffing,
      onSnapshotClick,
      onLoadMoreSiblings,
      onViewSourceInDebugger,
      view,
    } = this.props;

    if (!diffing && !snapshot) {
      return this._renderInitial(onSnapshotClick);
    }

    const state = getState(view, snapshot, diffing);
    const statusText = getStateStatusText(state, diffing);

    if (shouldDisplayStatus(state, view, snapshot)) {
      return this._renderStatus(state, statusText, diffing);
    }

    const error = getError(snapshot, diffing);
    if (error) {
      return this._renderError(state, statusText, error);
    }

    if (view === viewState.CENSUS || view === viewState.DIFFING) {
      const census = view === viewState.CENSUS
        ? snapshot.census
        : diffing.census;
      if (!census) {
        return this._renderStatus(state, statusText, diffing);
      }
      return this._renderCensus(state, census, diffing, onViewSourceInDebugger);
    }

    if (view === viewState.TREE_MAP) {
      return this._renderTreeMap(state, snapshot.treeMap);
    }

    assert(view === viewState.DOMINATOR_TREE,
           "If we aren't in progress, looking at a census, or diffing, then we " +
           "must be looking at a dominator tree");
    assert(!diffing, "Should not have diffing");
    assert(snapshot.dominatorTree, "Should have a dominator tree");

    return this._renderDominatorTree(state, onViewSourceInDebugger, snapshot.dominatorTree,
                                     onLoadMoreSiblings);
  },

  /**
   * Render the heap view's container panel with the given contents inside of
   * it.
   *
   * @param {snapshotState|diffingState|dominatorTreeState} state
   * @param {...Any} contents
   */
  _renderHeapView(state, ...contents) {
    return dom.div(
      {
        id: "heap-view",
        "data-state": state
      },
      dom.div(
        {
          className: "heap-view-panel",
          "data-state": state,
        },
        ...contents
      )
    );
  },

  _renderInitial(onSnapshotClick) {
    return this._renderHeapView("initial", dom.button(
      {
        className: "devtools-toolbarbutton take-snapshot",
        onClick: onSnapshotClick,
        "data-standalone": true,
        "data-text-only": true,
      },
      L10N.getStr("take-snapshot")
    ));
  },

  _renderStatus(state, statusText, diffing) {
    let throbber = "";
    if (shouldDisplayThrobber(diffing)) {
      throbber = "devtools-throbber";
    }

    return this._renderHeapView(state, dom.span(
      {
        className: `snapshot-status ${throbber}`
      },
      statusText
    ));
  },

  _renderError(state, statusText, error) {
    return this._renderHeapView(
      state,
      dom.span({ className: "snapshot-status error" }, statusText),
      dom.pre({}, safeErrorString(error))
    );
  },

  _renderCensus(state, census, diffing, onViewSourceInDebugger) {
    assert(census.report, "Should not render census that does not have a report");

    if (!census.report.children) {
      const msg = diffing       ? L10N.getStr("heapview.no-difference")
                : census.filter ? L10N.getStr("heapview.none-match")
                : /* else */      L10N.getStr("heapview.empty");
      return this._renderHeapView(state, dom.div({ className: "empty" }, msg));
    }

    const contents = [];

    if (census.display.breakdown.by === "allocationStack"
        && census.report.children
        && census.report.children.length === 1
        && census.report.children[0].name === "noStack") {
      contents.push(dom.div({ className: "error no-allocation-stacks" },
                            L10N.getStr("heapview.noAllocationStacks")));
    }

    contents.push(CensusHeader());
    contents.push(Census({
      onViewSourceInDebugger,
      diffing,
      census,
      onExpand: node => this.props.onCensusExpand(census, node),
      onCollapse: node => this.props.onCensusCollapse(census, node),
      onFocus: node => this.props.onCensusFocus(census, node),
    }));

    return this._renderHeapView(state, ...contents);
  },

  _renderTreeMap(state, treeMap) {
    return this._renderHeapView(
      state,
      TreeMap({ treeMap })
    );
  },

  _renderDominatorTree(state, onViewSourceInDebugger, dominatorTree, onLoadMoreSiblings) {
    const tree = dom.div(
      {
        className: "vbox",
        style: {
          overflowY: "auto"
        }
      },
      DominatorTreeHeader(),
      DominatorTree({
        onViewSourceInDebugger,
        dominatorTree,
        onLoadMoreSiblings,
        onExpand: this.props.onDominatorTreeExpand,
        onCollapse: this.props.onDominatorTreeCollapse,
        onFocus: this.props.onDominatorTreeFocus,
      })
    );

    const shortestPaths = ShortestPaths({
      graph: dominatorTree.focused
        ? dominatorTree.focused.shortestPaths
        : null
    });

    return this._renderHeapView(
      state,
      HSplitBox({
        start: tree,
        end: shortestPaths,
        startWidth: this.props.sizes.shortestPathsSize,
        onResize: this.props.onShortestPathsResize,
      })
    );
  },
});
