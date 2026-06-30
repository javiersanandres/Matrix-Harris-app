# Matrix-Harris App

A desktop application for creating, editing and automatically laying out **Harris Matrix diagrams**, developed as a Computer Science Engineering thesis (TFG) at Universidad Autónoma de Madrid.

## What is a Harris Matrix?

A Harris Matrix is a stratigraphic recording method, devised by archaeologist Edward Cecil Harris, used to organize and visualize the chronological and depositional relationships between the stratigraphic units identified during an archaeological excavation. It shows which layers of sediment lie above, below, or alongside others, letting archaeologists reconstruct the temporal sequence of events at a site.

## The problem

Drawing Harris Matrix diagrams by hand is slow and error-prone, especially for large or complex excavations. The difficulty becomes particularly acute when a research team excavates several separate areas of the same site and later needs to **merge** the individual diagrams into a single, coherent global picture (for example, when the same volcanic ash layer appears in different excavation zones and must be linked across them). No existing tool — free or commercial — properly supported this merging workflow, which motivated the development of this application.

## What this application does

Matrix-Harris App lets users interactively build, edit and merge Harris Matrix diagrams while the layout is recalculated automatically after every change, keeping the drawing clear, readable and visually consistent. Concretely, it supports:

- Creating and managing multiple diagrams within a project, plus a special **joint diagram** where individual diagrams can be combined side by side and connected to one another.
- Creating, renaming, deleting and merging stratigraphic units (nodes), as well as creating and editing the relationships between them (hyperedges), including operations such as adding a new parent/child, inserting a node inside an existing connection, or adding extra origins/destinations to a connection.
- Automatic, real-time recalculation of the diagram's visual layout after every edit, with manual "minimize crossings" and node-repositioning controls available to the user.
- Undo/redo of every editing operation.
- Saving and loading complete projects (all diagrams plus the joint diagram) to and from disk.

## The underlying problem, in computer-science terms

Behind the archaeological terminology, a Harris Matrix is formally a **dynamic, hierarchical, directed hypergraph with orthogonal hyperedge routing**:

- It is a **hypergraph** rather than a plain graph because a single stratigraphic relationship can connect several origin units to several destination units at once (ordinary binary edges cannot express this).
- It is **hierarchical**: nodes are organized into layers according to stratigraphic depth, and every hyperedge points strictly from a shallower layer to a deeper one.
- Its hyperedges must be drawn with **orthogonal routing through ports**: connections are made of horizontal and vertical segments only, attached to fixed connection points (ports) on the top/bottom of each node box, without overlaps between segments.

Producing a clear, automatically-updated drawing that respects all of this is a much harder problem than it looks: each of the required sub-problems — ordering nodes to minimize edge crossings, assigning horizontal coordinates, ordering and routing hyperedges, assigning ports, and avoiding overlaps — is individually NP-hard. The application solves this by adapting and extending a layout pipeline based on heuristics and classical graph-drawing algorithms (including a global sifting-based crossing minimization, the Brandes–Köpf horizontal coordinate assignment algorithm, and a mixed-integer program for hyperedge ordering), tuned to work incrementally so the diagram can be updated smoothly as the user edits it, instead of being recomputed from scratch every time.

## Project structure

The codebase is split into clearly separated layers:

- **`hypergraph_logic/`** — the domain layer: the hierarchical hypergraph data structure, its structural invariants (acyclicity, the Hasse-diagram property, long-edge handling), and the full automatic layout pipeline (crossing minimization, coordinate assignment, port assignment, etc.). This layer has no dependency on the GUI and could in principle be reused by any other front end.
- **`app/editors/`** — the application layer: editors that expose hypergraph-editing operations together with persistence (JSON serialization) and the transactional undo/redo mechanism.
- **`app/ui/`** — the presentation layer: the Qt-based graphical interface that renders the diagrams and lets the user interact with them.

## Project status

This is the result of a university thesis project. A full technical write-up — covering the theoretical background, the complete algorithmic design, the architecture, and the validation/usability results — is available as an accompanying PDF document.
