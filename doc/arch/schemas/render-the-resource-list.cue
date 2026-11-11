// DDD role: ValueObject

package schemas

// #RowRole is the closed set a row renders. The field mask of a ChangeSet names
// a subset (feature 005), so a role and a mask entry share a vocabulary.
#RowRole: "name" | "namespace" | "status" | "age"

// #RoleIndex is the integer a model role is registered under. Closed so a
// delegate and the model cannot drift.
#RoleIndex: {
    name:      int | *257
    namespace: int | *258
    status:    int | *259
    age:       int | *260
}

// #RowColumn is how one role is shown.
#RowColumn: {
    role:  #RowRole
    width: int & >0
}

// #ViewConfig is the fixed part of the view. Uniformity is stated, not assumed.
#ViewConfig: {
    layoutBatched:     bool | *true
    batchSize:         int & >0 | *100
    uniformItemSizes:  bool | *true
    headerResizeFixed: bool | *true
    columns: [...#RowColumn]
}

// #RowFootprint bounds the model itself, not the store.
#RowFootprint: {
    modelBytes: int & >=0 | *4096
    rows:       int & >=0
}
