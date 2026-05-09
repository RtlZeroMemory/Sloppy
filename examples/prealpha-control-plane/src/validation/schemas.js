export const validationShapes = Object.freeze({
    projectCreate: ["slug", "name"],
    appCreate: ["projectId", "name"],
    buildCreate: ["commit"],
    deploymentCreate: ["buildId"],
});
