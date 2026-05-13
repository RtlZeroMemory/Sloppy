if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CMAKE_BINARY_DIR)
    message(FATAL_ERROR "CMAKE_BINARY_DIR is required")
endif()
if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()
if(NOT DEFINED SLOPPYC_EXECUTABLE)
    message(FATAL_ERROR "SLOPPYC_EXECUTABLE is required")
endif()

set(work_dir "${CMAKE_BINARY_DIR}/orm-package-metadata")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}/src")
file(WRITE "${work_dir}/sloppy.json" [=[{
  "entry": "src/app.ts"
}
]=])
file(WRITE "${work_dir}/src/app.ts" [=[
import { Results, Sloppy, column, relation, table } from "sloppy";

const Teams = table("teams", {
  id: column.uuid().primaryKey(),
  name: column.text().notNull(),
});

const Users = table("users", {
  id: column.uuid().primaryKey(),
  teamId: column.uuid().notNull().references(() => Teams.id),
  email: column.text().notNull().unique(),
});

relation(Users, ({ one }) => ({
  team: one(Teams, {
    local: Users.teamId,
    foreign: Teams.id,
  }),
}));

const app = Sloppy.create();
app.mapGet("/users", () => Results.json([]));
export default app;
]=])

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}" "${SLOPPY_CLI}"
            package --format json
    WORKING_DIRECTORY "${work_dir}"
    TIMEOUT 180
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_stdout
    ERROR_VARIABLE package_stderr)

if(NOT package_result EQUAL 0)
    message(FATAL_ERROR "ORM package metadata fixture failed\nstdout:\n${package_stdout}\nstderr:\n${package_stderr}")
endif()
if(NOT package_stdout MATCHES "\"packaged\"[ \t\r\n]*:[ \t\r\n]*true")
    message(FATAL_ERROR "ORM package metadata fixture did not report package success\nstdout:\n${package_stdout}")
endif()

set(plan_path "${work_dir}/.sloppy/package/artifacts/app.plan.json")
if(NOT EXISTS "${plan_path}")
    message(FATAL_ERROR "ORM package metadata fixture did not emit packaged Plan")
endif()

file(READ "${plan_path}" plan_json)
foreach(required IN ITEMS
        "\"orm\"[ \t\r\n]*:[ \t\r\n]*true"
        "\"orm\"[ \t\r\n]*:[ \t\r\n]*\\{"
        "\"tables\"[ \t\r\n]*:[ \t\r\n]*\\["
        "\"relations\"[ \t\r\n]*:[ \t\r\n]*\\["
        "\"name\"[ \t\r\n]*:[ \t\r\n]*\"users\""
        "\"name\"[ \t\r\n]*:[ \t\r\n]*\"team\"")
    if(NOT plan_json MATCHES "${required}")
        message(FATAL_ERROR "packaged Plan is missing ORM metadata pattern ${required}\n${plan_json}")
    endif()
endforeach()
