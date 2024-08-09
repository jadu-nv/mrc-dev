import { expect } from "@jest/globals";
import { ResourceActualStatus, ResourceStatus } from "@mrc/proto/mrc/protos/architect_state";
import { pipelineDefinitionsAdd } from "@mrc/server/store/slices/pipelineDefinitionsSlice";
import {
   pipelineInstancesAdd,
   pipelineInstancesRemove,
   pipelineInstancesSelectAll,
   pipelineInstancesSelectById,
   pipelineInstancesSelectTotal,
   pipelineInstancesUpdateResourceActualState,
} from "@mrc/server/store/slices/pipelineInstancesSlice";
import {
   segmentInstancesRemove,
   segmentInstancesSelectByPipelineId,
   segmentInstancesUpdateResourceActualState,
} from "@mrc/server/store/slices/segmentInstancesSlice";
import { executor, pipeline, pipeline_def, segments, worker, pipeline_mappings } from "@mrc/tests/defaultObjects";
import assert from "assert";

import { resourceUpdateActualState } from "@mrc/server/store/slices/resourceActions";
import { manifoldInstancesSelectByPipelineId } from "@mrc/server/store/slices/manifoldInstancesSlice";
import { pipelineDefinitionsSetMapping } from "@mrc/server/store/slices/pipelineDefinitionsSlice";
import { connectionsAdd, connectionsDropOne } from "@mrc/server/store/slices/connectionsSlice";
import { workersAdd } from "@mrc/server/store/slices/workersSlice";
import { RootStore, setupStore } from "@mrc/server/store/store";

let store: RootStore;

// Get a clean store each time
beforeEach(() => {
   store = setupStore();
});

describe("Empty", () => {
   test("Select All", () => {
      expect(pipelineInstancesSelectAll(store.getState())).toHaveLength(0);
   });

   test("Total", () => {
      expect(pipelineInstancesSelectTotal(store.getState())).toBe(0);
   });

   test("Remove", () => {
      assert.throws(() => store.dispatch(pipelineInstancesRemove(pipeline)));
   });

   test("Before Connection", () => {
      assert.throws(() => {
         store.dispatch(pipelineInstancesAdd(pipeline));
      });
   });

   test("Before Definition", () => {
      store.dispatch(connectionsAdd(executor));

      assert.throws(() => {
         store.dispatch(pipelineInstancesAdd(pipeline));
      });
   });
});

describe("Single", () => {
   beforeEach(async () => {
      store.dispatch(connectionsAdd(executor));

      store.dispatch(pipelineDefinitionsAdd(pipeline_def));

      store.dispatch(
         pipelineDefinitionsSetMapping({
            definition_id: pipeline_def.id,
            mapping: pipeline_mappings[executor.id],
         })
      );

      store.dispatch(pipelineInstancesAdd(pipeline));

      // // Make sure they are all indicated as created
      // await store.dispatch(
      //    resourceUpdateActualState("PipelineInstances", pipeline.id, ResourceActualStatus.Actual_Created)
      // );

      // for (const m of manifolds) {
      //    await store.dispatch(
      //       resourceUpdateActualState("ManifoldInstances", m.id, ResourceActualStatus.Actual_Created)
      //    );
      // }

      // for (const s of segments) {
      //    await store.dispatch(resourceUpdateActualState("SegmentInstances", s.id, ResourceActualStatus.Actual_Created));
      // }
   });

   test("Select All", () => {
      const found = pipelineInstancesSelectAll(store.getState());

      expect(found).toHaveLength(1);

      expect(found[0]).toHaveProperty("id", pipeline.id);
      expect(found[0]).toHaveProperty("definitionId", pipeline.definitionId);
      expect(found[0]).toHaveProperty("executorId", pipeline.executorId);
      expect(found[0]).toHaveProperty("segmentIds", []);
   });

   test("Select One", () => {
      const found = pipelineInstancesSelectById(store.getState(), pipeline.id);

      expect(found).toHaveProperty("id", pipeline.id);
      expect(found).toHaveProperty("definitionId", pipeline.definitionId);
      expect(found).toHaveProperty("executorId", pipeline.executorId);
      expect(found).toHaveProperty("segmentIds", []);
   });

   test("Total", () => {
      expect(pipelineInstancesSelectTotal(store.getState())).toBe(1);
   });

   test("Add Duplicate", () => {
      assert.throws(() => store.dispatch(pipelineInstancesAdd(pipeline)));
   });

   it("Remove Valid ID", () => {
      store.dispatch(pipelineInstancesRemove(pipeline));

      expect(pipelineInstancesSelectAll(store.getState())).toHaveLength(0);
   });

   test("Remove Unknown ID", () => {
      assert.throws(() =>
         store.dispatch(
            pipelineInstancesRemove({
               ...pipeline,
               id: "9999",
            })
         )
      );
   });

   test("Remove Incorrect Machine ID", () => {
      assert.throws(() =>
         store.dispatch(
            pipelineInstancesRemove({
               ...pipeline,
               executorId: "1",
            })
         )
      );
   });

   test("Drop Connection", async () => {
      await store.dispatch(connectionsDropOne({ id: executor.id }));

      expect(pipelineInstancesSelectAll(store.getState())).toHaveLength(0);
   });

   describe("With Segment Instance", () => {
      beforeEach(async () => {
         // Add a worker first, then a segment
         store.dispatch(workersAdd(worker));

         // Update the instance state to ready and the instances should auto assign
         await store.dispatch(
            resourceUpdateActualState("PipelineInstances", pipeline.id, ResourceActualStatus.Actual_Created)
         );

         const manifolds = manifoldInstancesSelectByPipelineId(store.getState(), pipeline.id);

         // Update all manifold states as well to automatically generate segment instances
         for (const m of manifolds) {
            await store.dispatch(
               resourceUpdateActualState("ManifoldInstances", m.id, ResourceActualStatus.Actual_Created)
            );
         }
      });

      test("Contains Instance", () => {
         const found = pipelineInstancesSelectById(store.getState(), pipeline.id);

         segmentInstancesSelectByPipelineId(store.getState(), pipeline.id).forEach((s) => {
            expect(found!.segmentIds).toContain(s.id);
         });
      });

      test("Remove Segment", () => {
         const found_segments = segmentInstancesSelectByPipelineId(store.getState(), pipeline.id);

         found_segments.forEach((s) =>
            store.dispatch(
               segmentInstancesUpdateResourceActualState({ resource: s, status: ResourceActualStatus.Actual_Destroyed })
            )
         );
         found_segments.forEach((s) => store.dispatch(segmentInstancesRemove(s)));

         const found = pipelineInstancesSelectById(store.getState(), pipeline.id);

         found_segments.forEach((s) => expect(found?.segmentIds).not.toContain(s.id));

         // Then remove the pipeline
         store.dispatch(pipelineInstancesRemove(pipeline));

         expect(pipelineInstancesSelectAll(store.getState())).toHaveLength(0);
      });

      test("Remove Pipeline Before Segment", () => {
         assert.throws(() => {
            // Remove the pipeline with running segments
            store.dispatch(pipelineInstancesRemove(pipeline));
         });
      });
   });
});